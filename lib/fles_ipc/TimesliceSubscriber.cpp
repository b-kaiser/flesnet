// Copyright 2014 Jan de Cuveland <cmail@cuveland.de>

#include "TimesliceSubscriber.hpp"

namespace fles {

TimesliceSubscriber::TimesliceSubscriber(const std::string& address,
                                         uint32_t hwm) {
  subscriber_.setsockopt(ZMQ_RCVHWM, hwm);
  subscriber_.connect(address.c_str());
  subscriber_.setsockopt(ZMQ_SUBSCRIBE, nullptr, 0);
}

fles::StorableTimeslice* TimesliceSubscriber::do_get() {
  if (eos_flag) {
    return nullptr;
  }

  zmq::message_t message;
  subscriber_.recv(&message);

  boost::iostreams::basic_array_source<char> device(
      static_cast<char*>(message.data()), message.size());
  boost::iostreams::stream<boost::iostreams::basic_array_source<char>> s(
      device);
  boost::archive::binary_iarchive ia(s);

  fles::StorableTimeslice* sts = nullptr;
  try {
    sts = new fles::StorableTimeslice();
    ia >> *sts;
  } catch (boost::archive::archive_exception& e) {
    delete sts;
    eos_flag = true;
    return nullptr;
  }
  return sts;
}

} // namespace fles
