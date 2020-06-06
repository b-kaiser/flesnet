#pragma once

#include "ItemWorkerProtocol.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <deque>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <zmq.hpp>
#include <zmq_addon.hpp>

/*

Each worker ("ItemWorker") connects to the broker ("ItemDistributor").
It starts the communication by sending a REGISTER message. After that, the
broker can send a WORK_ITEM message at any time, and the worker can send a
COMPLETION message at any time. In addition, the broker sends HEARTBEAT messages
regularly if there is no outstanding WORK_ITEM. This allows the worker to detect
that the broker has died, in which case the worker closes the socket and opens a
new connection.

The broker keeps track of all connected workers. It detects that a worker has
died by the disconnect notification. When this happens, it releases all
outstanding WORK_ITEMs for that worker and stops sending messages to it.

The REGISTER message contains a specification of the type of items the worker
wants to receive (stride, offset). It also specifies the queueing mode:
- fully asynchronous, receive all, don't skip
  All matching items are immediately sent as a WORK_ITEM to the send queue, or,
if this fails, put on an internal waiting_items queue.
- prebuffer one
  The broker keeps the newest matching item in an internal 1-item queue if the
worker is not idle. The item is sent once the broker receives the outstanding
completion from the worker.
- skip
  The broker keeps no item queue for this worker. It only sends the item
immediately if the worker is idle.

Work items are received from an exclusive producer client through a ZMQ_PAIR
socket.

....
on new item n:
make new item object with shared_ptr and proper destructor (queue a message on
destruction) and item id member

  for all connections:

    if item "fits" connection:

      enqueue it

      if client is idle, send it immediately

on reception of a completion:
  remove item from outstanding_items

*/

// Request every item with sequence number n for which exists m in N:
// n = m * stride + offset

class Item {
public:
  Item(std::vector<ItemID>& completed_items,
       ItemID id,
       const std::string& payload)
      : completed_items_(completed_items), id_(id), payload_(payload) {}

  ItemID id() const { return id_; }

  const std::string& payload() const { return payload_; }

  ~Item() { completed_items_.emplace_back(id_); }

private:
  std::vector<ItemID>& completed_items_;
  const ItemID id_;
  const std::string payload_;
};

struct Worker {
  size_t stride;
  size_t offset;
  WorkerQueuePolicy queue_policy;
  std::string client_name;

  std::deque<std::shared_ptr<Item>> waiting_items;
  std::deque<std::shared_ptr<Item>> outstanding_items;
  std::chrono::system_clock::time_point next_heartbeat_time;

  bool wants_item(ItemID id) const { return id % stride == offset; }
};

class ItemDistributor {
public:
  ItemDistributor(const std::string& producer_address,
                  const std::string& worker_address) {
    generator_socket_.bind(producer_address);
    generator_socket_.set(zmq::sockopt::linger, 0);
    worker_socket_.set(zmq::sockopt::router_mandatory, 1);
    worker_socket_.set(zmq::sockopt::router_notify, ZMQ_NOTIFY_DISCONNECT);
    worker_socket_.bind(worker_address);
    worker_socket_.set(zmq::sockopt::linger, 0);
  }

  void operator()() {
    zmq::active_poller_t poller;
    poller.add(generator_socket_, zmq::event_flags::pollin,
               [&](zmq::event_flags e) { on_generator_pollin(); });
    poller.add(worker_socket_, zmq::event_flags::pollin,
               [&](zmq::event_flags e) { on_worker_pollin(); });

    while (true) {
      poller.wait(std::chrono::milliseconds{1000});
      send_heartbeat();
    }
  }

  void stop() {}

  ~ItemDistributor() {
    // TODO: sensible clean-up
  }

private:
  // Send heartbeat messages to workers that have been idle for a while
  void send_heartbeat() {
    for (auto& [identity, w] : workers_) {
      if (w->outstanding_items.empty()) {
        // TODO: ... AND some timing things ...
        send_heartbeat(identity);
      }
    }
  }

  void send_pending_completions() {
    try {
      for (auto item : completed_items_) {
        generator_socket_.send(zmq::buffer(std::to_string(item)));
      }
    } catch (zmq::error_t& error) {
      std::cout << "ERROR: " << error.what() << std::endl;
    }
    completed_items_.clear();
  }

  std::shared_ptr<Item> receive_producer_item() {
    zmq::multipart_t message(generator_socket_);

    // Receive item ID
    ItemID id = std::stoull(message.popstr());

    // Receive optional item payload
    std::string payload;
    if (!message.empty()) {
      payload = message.popstr();
    }

    return std::make_shared<Item>(completed_items_, id, payload);
  }

  // Handle incoming message (work item) from the generator
  void on_generator_pollin() {
    auto new_item = receive_producer_item();

    // Distribute the new work item
    for (auto& [identity, w] : workers_) {
      if (w->wants_item(new_item->id())) {
        if (w->queue_policy == WorkerQueuePolicy::PrebufferOne) {
          w->waiting_items.clear();
        }
        if (w->outstanding_items.empty()) {
          // The worker is idle, send the item immediately
          w->outstanding_items.push_back(new_item);
          send_work_item(identity, *new_item);
        } else {
          // The worker is busy, enqueue the item
          if (w->queue_policy != WorkerQueuePolicy::Skip) {
            w->waiting_items.push_back(new_item);
          }
        }
      }
    }
    new_item = nullptr;
    // A pending completion could occur here if this item is not sent to any
    // worker, so...
    send_pending_completions();
  }

  // Handle incoming message from a worker
  void on_worker_pollin() {
    zmq::multipart_t message(worker_socket_);
    assert(message.size() >= 2);       // Multipart format ensured by ZMQ
    assert(message.at(0).size() > 0);  // for ROUTER sockets
    assert(message.at(1).size() == 0); //

    std::string identity = message.peekstr(1);

    if (message.size() == 2) {
      // Handle ZMQ worker disconnect notification
      std::cout << "Info: received disconnect notification" << std::endl;
      if (workers_.erase(identity) == 0) {
        std::cerr << "Error: disconnect from unknown worker" << std::endl;
      }
    } else {
      // Handle general message from a worker
      std::string message_string = message.peekstr(3);
      if (message_string.rfind("REGISTER ", 0) == 0) {
        // Handle new worker registration
        auto c = std::make_unique<Worker>();
        std::string command;
        std::stringstream s(message_string);
        s >> command >> c->stride >> c->offset >> c->queue_policy >>
            c->client_name;
        assert(!s.fail());
        workers_[identity] = std::move(c);
      } else if (message_string.rfind("COMPLETE ", 0) == 0) {
        // Handle worker completion message
        auto& c = workers_.at(identity);
        std::string command;
        ItemID id;
        std::stringstream s(message_string);
        s >> command >> id;
        assert(!s.fail());
        // Find the corresponding outstanding item object and delete it
        auto it = std::find_if(
            std::begin(c->outstanding_items), std::end(c->outstanding_items),
            [id](const std::shared_ptr<Item>& i) { return i->id() == id; });
        assert(it != std::end(c->outstanding_items));
        c->outstanding_items.erase(it);
        // Send next item if available
        if (!c->waiting_items.empty()) {
          auto item = c->waiting_items.front();
          c->waiting_items.pop_front();
          c->outstanding_items.push_back(item);
          send_work_item(identity, *item);
        }
      }
    }
    send_pending_completions();
  }

  void send_work_item(const std::string& identity, const Item& item) {
    // Prepare first two message parts as required for a ROUTER socket
    zmq::multipart_t message(identity);
    message.add(zmq::message_t(0));

    // Prepare message contents
    message.addstr("WORK_ITEM " + std::to_string(item.id()));
    if (!item.payload().empty()) {
      message.addstr(item.payload());
    }

    // Send the message
    if (!message.send(worker_socket_)) {
      std::cerr << "Error: message send failed";
    }
  }

  void send_disconnect(const std::string& identity) {
    // Prepare first two message parts as required for a ROUTER socket
    zmq::multipart_t message(identity);
    message.add(zmq::message_t(0));

    // Prepare message contents
    message.addstr("DISCONNECT");

    // Send the message
    if (!message.send(worker_socket_)) {
      std::cerr << "Error: message send failed";
    }
  }

  void send_heartbeat(const std::string& identity) {
    // Prepare first two message parts as required for a ROUTER socket
    zmq::multipart_t message(identity);
    message.add(zmq::message_t(0));

    // Prepare message contents
    message.addstr("HEARTBEAT");

    // Send the message
    if (!message.send(worker_socket_)) {
      std::cerr << "Error: message send failed";
    }
  }

  zmq::context_t context_{1};
  zmq::socket_t generator_socket_{context_, ZMQ_PAIR};
  zmq::socket_t worker_socket_{context_, ZMQ_ROUTER};
  std::vector<ItemID> completed_items_;
  std::map<std::string, std::unique_ptr<Worker>> workers_;
};
