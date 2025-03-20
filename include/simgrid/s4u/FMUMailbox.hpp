/* Copyright (c) 2006-2024. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef SIMGRID_S4U_FMUMAILBOX_HPP
#define SIMGRID_S4U_FMUMAILBOX_HPP

#include <simgrid/forward.h>

#include <simgrid/Exception.hpp>
#include <simgrid/s4u/Actor.hpp>
#include <simgrid/s4u/Comm.hpp>
#include <smpi/forward.hpp>

#include <memory>
#include <string>

namespace simgrid::s4u {

enum class FmuMappingPolicy { NONE, RANDOM, INCREMENTAL, LEAST_S };

class FMUUnit;

/** @brief Mailboxes: Network rendez-vous points. */
class XBT_PUBLIC FMUMailbox {
#ifndef DOXYGEN
  friend Comm;
  friend smpi::Request;
  friend kernel::activity::MailboxImpl;
#endif

explicit FMUMailbox(const std::string& name) : mailbox_(simgrid::s4u::Mailbox::by_name(name)) {};
~FMUMailbox() = default;
  private:
  // FMU-specific data structures
  std::vector<FMUUnit> fmu_units;      // FMU pool (size = M)
  std::unordered_map<int, std::queue<smpi::Request*>> fmu_queues; // Queue per FMU
  FmuMappingPolicy mapping_policy;     // TRY_IDLE, INCREMENTAL, etc.
  simgrid::s4u::Mailbox* mailbox_;

  // Optical switch parameters
  double switching_latency = 5e-6;     // 5 μs switching time
  double ocm_latency = 3.2e-9;         // 3.2 ns OCM delay

  public:
    explicit FMUMailbox(const std::string& name) : mailbox_(simgrid::s4u::Mailbox::by_name(name)) {};
    
    // FMU-specific methods
    int fmuaquire(smpi::Request* req);          // Request FMU access
    void fmurelease(int fmu_index);       // Release FMU
    void set_mapping_policy(FmuMappingPolicy policy);
  };
} // namespace simgrid::s4u


#endif /* SIMGRID_S4U_MAILBOX_HPP */
