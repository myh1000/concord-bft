// Concord
//
// Copyright (c) 2018-2021 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License").
// You may not use this product except in compliance with the Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright
// notices and license terms. Your use of these subcomponents is subject to the
// terms and conditions of the subcomponent's license, as noted in the LICENSE
// file.

#pragma once

#include "concord.cmf.hpp"
#include "OpenTracing.hpp"
#include "kv_types.hpp"
#include "Replica.hpp"

namespace concord::reconfiguration {
enum ReconfigurationHandlerType : unsigned int { PRE, REGULAR, POST };
// The IReconfigurationHandler interface defines all message handler. It is
// tightly coupled with the messages inside ReconfigurationSmRequest in the
// message definition.
class IReconfigurationHandler {
 public:
  // Message handlers
  virtual bool handle(const concord::messages::WedgeCommand&, uint64_t, concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::WedgeStatusRequest&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::GetVersionCommand&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::DownloadCommand&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::DownloadStatusCommand&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::InstallCommand&, uint64_t, concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::InstallStatusCommand&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::KeyExchangeCommand&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::AddRemoveCommand&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::AddRemoveStatus&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::LatestPrunableBlockRequest&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::PruneStatusRequest&,
                      uint64_t,
                      concord::messages::ReconfigurationResponse&) {
    return true;
  }
  virtual bool handle(const concord::messages::PruneRequest&, uint64_t, concord::messages::ReconfigurationResponse&) {
    return true;
  }
  // The verification method is pure virtual as all subclasses has to define how they verify the reconfiguration
  // requests.
  virtual bool verifySignature(const std::string& data, const std::string& signature) const = 0;
  virtual ~IReconfigurationHandler() = default;
};

}  // namespace concord::reconfiguration
