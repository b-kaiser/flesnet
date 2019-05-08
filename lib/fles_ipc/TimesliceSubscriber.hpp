// Copyright 2014 Jan de Cuveland <cmail@cuveland.de>
/// \file
/// \brief Defines the fles::TimesliceSubscriber class.
#pragma once

#include "StorableTimeslice.hpp"
#include "TimesliceSource.hpp"
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <string>
#include <zmq.hpp>

namespace fles {
/**
 * \brief The TimesliceSubscriber class receives serialized timeslice data sets
 * from a zeromq socket.
 */
class TimesliceSubscriber : public TimesliceSource {
public:
  /// Construct timeslice subscriber receiving from given ZMQ address.
  explicit TimesliceSubscriber(const std::string& address, uint32_t hwm = 1);

  /// Delete copy constructor (non-copyable).
  TimesliceSubscriber(const TimesliceSubscriber&) = delete;
  /// Delete assignment operator (non-copyable).
  void operator=(const TimesliceSubscriber&) = delete;

  ~TimesliceSubscriber() override = default;

  /**
   * \brief Retrieve the next item.
   *
   * This function blocks if the next item is not yet available.
   *
   * \return pointer to the item, or nullptr if end-of-file
   */
  std::unique_ptr<StorableTimeslice> get() {
    return std::unique_ptr<StorableTimeslice>(do_get());
  };

  bool eos() const override { return eos_flag; }

private:
  StorableTimeslice* do_get() override;

  zmq::context_t context_{1};
  zmq::socket_t subscriber_{context_, ZMQ_SUB};

  bool eos_flag = false;
};

} // namespace fles
