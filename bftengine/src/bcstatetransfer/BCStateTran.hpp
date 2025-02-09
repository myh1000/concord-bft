// Concord
//
// Copyright (c) 2018 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License").
// You may not use this product except in compliance with the Apache 2.0
// License.
//
// This product may include a number of subcomponents with separate copyright
// notices and license terms. Your use of these subcomponents is subject to the
// terms and conditions of the subcomponent's license, as noted in the LICENSE
// file.

#pragma once

#include <set>
#include <map>
#include <chrono>
#include <random>
#include <cassert>
#include <iostream>
#include <string>
#include <array>
#include <cstdint>
#include <optional>

#include "Logger.hpp"
#include "SimpleBCStateTransfer.hpp"
#include "IStateTransfer.hpp"
#include "DataStore.hpp"
#include "MsgsCertificate.hpp"
#include "Messages.hpp"
#include "STDigest.hpp"
#include "Metrics.hpp"
#include "SourceSelector.hpp"
#include "callback_registry.hpp"
#include "Handoff.hpp"
#include "SysConsts.hpp"
#include "throughput.hpp"
#include "diagnostics.h"
#include "performance_handler.h"

using std::set;
using std::map;
using std::string;
using concordMetrics::StatusHandle;
using concordMetrics::GaugeHandle;
using concordMetrics::CounterHandle;
using concord::util::Throughput;

namespace bftEngine::bcst::impl {

class BCStateTran : public IStateTransfer {
 public:
  BCStateTran(const Config& config, IAppState* const stateApi, DataStore* ds = nullptr);

  ~BCStateTran() override;

  ///////////////////////////////////////////////////////////////////////////
  // IStateTransfer methods
  ///////////////////////////////////////////////////////////////////////////

  void init(uint64_t maxNumOfRequiredStoredCheckpoints,
            uint32_t numberOfRequiredReservedPages,
            uint32_t sizeOfReservedPage) override;
  void startRunning(IReplicaForStateTransfer* r) override;
  void stopRunning() override;
  bool isRunning() const override;

  void createCheckpointOfCurrentState(uint64_t checkpointNumber) override;

  void markCheckpointAsStable(uint64_t checkpointNumber) override;

  void getDigestOfCheckpoint(uint64_t checkpointNumber, uint16_t sizeOfDigestBuffer, char* outDigestBuffer) override;

  void startCollectingState() override;

  bool isCollectingState() const override;

  uint32_t numberOfReservedPages() const override;

  uint32_t sizeOfReservedPage() const override;

  bool loadReservedPage(uint32_t reservedPageId, uint32_t copyLength, char* outReservedPage) const override;

  void saveReservedPage(uint32_t reservedPageId, uint32_t copyLength, const char* inReservedPage) override;
  void zeroReservedPage(uint32_t reservedPageId) override;

  void onTimer() override { timerHandler_(); };
  void handleStateTransferMessage(char* msg, uint32_t msgLen, uint16_t senderId) override {
    messageHandler_(msg, msgLen, senderId);
  };

  std::string getStatus() override;

  void addOnTransferringCompleteCallback(std::function<void(uint64_t)>) override;

  void setEraseMetadataFlag() override { psd_->setEraseDataStoreFlag(); }

 protected:
  // handling messages from other context
  std::function<void(char*, uint32_t, uint16_t)> messageHandler_;
  void handleStateTransferMessageImp(char* msg, uint32_t msgLen, uint16_t senderId);
  void handoffMsg(char* msg, uint32_t msgLen, uint16_t senderId) {
    handoff_->push(std::bind(&BCStateTran::handleStateTransferMessageImp, this, msg, msgLen, senderId));
  }

  // handling timer from other context
  std::function<void()> timerHandler_;
  void onTimerImp();
  void handoffTimer() { handoff_->push(std::bind(&BCStateTran::onTimerImp, this)); }

  ///////////////////////////////////////////////////////////////////////////
  // Constants
  ///////////////////////////////////////////////////////////////////////////
  static constexpr uint64_t kMaxNumOfStoredCheckpoints = 10;
  static constexpr uint16_t kMaxVBlocksInCache = 28;                    // TBD
  static constexpr uint32_t kResetCount_AskForCheckpointSummaries = 4;  // TBD

  ///////////////////////////////////////////////////////////////////////////
  // External interfaces
  ///////////////////////////////////////////////////////////////////////////

  IAppState* const as_;
  std::shared_ptr<DataStore> psd_;
  ///////////////////////////////////////////////////////////////////////////
  // Management and general data
  ///////////////////////////////////////////////////////////////////////////
  const Config config_;
  const set<uint16_t> replicas_;
  const uint32_t maxVBlockSize_;
  const uint32_t maxItemSize_;
  const uint32_t maxNumOfChunksInAppBlock_;
  const uint32_t maxNumOfChunksInVBlock_;

  uint64_t maxNumOfStoredCheckpoints_;
  uint64_t numberOfReservedPages_;

  std::atomic<bool> running_ = false;
  std::unique_ptr<concord::util::Handoff> handoff_;
  IReplicaForStateTransfer* replicaForStateTransfer_ = nullptr;

  char* buffer_;  // temporary buffer

  // random generator
  std::random_device randomDevice_;
  std::mt19937 randomGen_;

  // get ST client or ST server logger according to getFetchingState()
  logging::Logger& getLogger() const { return (psd_->getIsFetchingState() ? ST_DST_LOG : ST_SRC_LOG); }

  ///////////////////////////////////////////////////////////////////////////
  // Unique message IDs
  ///////////////////////////////////////////////////////////////////////////

  uint64_t uniqueMsgSeqNum();
  bool checkValidityAndSaveMsgSeqNum(uint16_t replicaId, uint64_t msgSeqNum);

  // used to computed my last msg sequence number
  uint64_t lastMilliOfUniqueFetchID_ = 0;
  uint32_t lastCountOfUniqueFetchID_ = 0;

  // my last msg sequence number
  uint64_t lastMsgSeqNum_ = 0;

  // msg sequence number from other replicas
  // map from replica id to its last MsgSeqNum
  map<uint16_t, uint64_t> lastMsgSeqNumOfReplicas_;

  ///////////////////////////////////////////////////////////////////////////
  // State
  ///////////////////////////////////////////////////////////////////////////

  // Public for testing and status
 public:
  enum class FetchingState { NotFetching, GettingCheckpointSummaries, GettingMissingBlocks, GettingMissingResPages };

  static string stateName(FetchingState fs);

  FetchingState getFetchingState() const;
  bool isFetching() const;

  inline std::string getSequenceNumber(uint16_t replicaId, uint64_t seqNum, uint16_t = 0, uint64_t = 0);

  ///////////////////////////////////////////////////////////////////////////
  // Send messages
  ///////////////////////////////////////////////////////////////////////////
 protected:
  void sendToAllOtherReplicas(char* msg, uint32_t msgSize);

  void sendAskForCheckpointSummariesMsg();

  void sendFetchBlocksMsg(uint64_t firstRequiredBlock,
                          uint64_t lastRequiredBlock,
                          int16_t lastKnownChunkInLastRequiredBlock);

  void sendFetchResPagesMsg(int16_t lastKnownChunkInLastRequiredBlock);

  ///////////////////////////////////////////////////////////////////////////
  // Message handlers
  ///////////////////////////////////////////////////////////////////////////

  bool onMessage(const AskForCheckpointSummariesMsg* m, uint32_t msgLen, uint16_t replicaId);
  bool onMessage(const CheckpointSummaryMsg* m, uint32_t msgLen, uint16_t replicaId);
  bool onMessage(const FetchBlocksMsg* m, uint32_t msgLen, uint16_t replicaId);
  bool onMessage(const FetchResPagesMsg* m, uint32_t msgLen, uint16_t replicaId);
  bool onMessage(const RejectFetchingMsg* m, uint32_t msgLen, uint16_t replicaId);
  bool onMessage(const ItemDataMsg* m, uint32_t msgLen, uint16_t replicaId);

  ///////////////////////////////////////////////////////////////////////////
  // cache that holds virtual blocks
  ///////////////////////////////////////////////////////////////////////////

  struct DescOfVBlockForResPages {
    uint64_t checkpointNum;
    uint64_t lastCheckpointKnownToRequester;

    // TOOD(GG): TBD
    bool operator<(const DescOfVBlockForResPages& rhs) const {
      if (checkpointNum != rhs.checkpointNum)
        return (checkpointNum < rhs.checkpointNum);
      else
        return (lastCheckpointKnownToRequester < rhs.lastCheckpointKnownToRequester);
    }
  };

  // map from DescOfVBlockForResPages to the virtual block
  map<DescOfVBlockForResPages, char*> cacheOfVirtualBlockForResPages;

  char* getVBlockFromCache(const DescOfVBlockForResPages& desc) const;
  void setVBlockInCache(const DescOfVBlockForResPages& desc, char* vBlock);
  char* createVBlock(const DescOfVBlockForResPages& desc);

  ///////////////////////////////////////////////////////////////////////////
  // The following is only used when the state is
  // FetchingState::GettingCheckpointSummaries
  ///////////////////////////////////////////////////////////////////////////

  uint64_t lastTimeSentAskForCheckpointSummariesMsg = 0;
  uint16_t retransmissionNumberOfAskForCheckpointSummariesMsg = 0;

  typedef MsgsCertificate<CheckpointSummaryMsg, false, false, true, CheckpointSummaryMsg> CheckpointSummaryMsgCert;

  // map from checkpintNum to CheckpointSummaryMsgCert
  map<uint64_t, CheckpointSummaryMsgCert*> summariesCerts;

  // map from replica Id to number of accepted CheckpointSummaryMsg messages
  map<uint16_t, uint16_t> numOfSummariesFromOtherReplicas;

  void clearInfoAboutGettingCheckpointSummary();

  void verifyEmptyInfoAboutGettingCheckpointSummary();

  ///////////////////////////////////////////////////////////////////////////
  // The following is only used when the state is GettingMissingBlocks
  // or GettingMissingResPages
  ///////////////////////////////////////////////////////////////////////////

  SourceSelector sourceSelector_;

  static const uint64_t ID_OF_VBLOCK_RES_PAGES = UINT64_MAX;

  uint64_t nextRequiredBlock_ = 0;
  STDigest digestOfNextRequiredBlock;

  struct compareItemDataMsg {
    bool operator()(const ItemDataMsg* l, const ItemDataMsg* r) const {
      if (l->blockNumber != r->blockNumber)
        return (l->blockNumber > r->blockNumber);
      else
        return (l->chunkNumber < r->chunkNumber);
    }
  };

  set<ItemDataMsg*, compareItemDataMsg> pendingItemDataMsgs;
  uint32_t totalSizeOfPendingItemDataMsgs = 0;

  string preferredReplicasToString();
  void clearAllPendingItemsData();
  void clearPendingItemsData(uint64_t untilBlock);
  bool getNextFullBlock(uint64_t requiredBlock,
                        bool& outBadDataDetected,
                        int16_t& outLastChunkInRequiredBlock,
                        char* outBlock,
                        uint32_t& outBlockSize,
                        bool isVBLock,
                        bool& outLastInBatch);

  bool checkBlock(uint64_t blockNum, const STDigest& expectedBlockDigest, char* block, uint32_t blockSize) const;

  bool checkVirtualBlockOfResPages(const STDigest& expectedDigestOfResPagesDescriptor,
                                   char* vblock,
                                   uint32_t vblockSize) const;

  void processData();

  void EnterGettingCheckpointSummariesState();
  set<uint16_t> allOtherReplicas();
  void SetAllReplicasAsPreferred();

  ///////////////////////////////////////////////////////////////////////////
  // Helper methods
  ///////////////////////////////////////////////////////////////////////////

  DataStore::CheckpointDesc createCheckpointDesc(uint64_t checkpointNumber, const STDigest& digestOfResPagesDescriptor);

  STDigest checkpointReservedPages(uint64_t checkpointNumber, DataStoreTransaction* txn);

  void deleteOldCheckpoints(uint64_t checkpointNumber, DataStoreTransaction* txn);

  ///////////////////////////////////////////////////////////////////////////
  // Consistency
  ///////////////////////////////////////////////////////////////////////////

  void checkConsistency(bool checkAllBlocks);
  void checkConfig();
  void checkFirstAndLastCheckpoint(uint64_t firstStoredCheckpoint, uint64_t lastStoredCheckpoint);
  void checkReachableBlocks(uint64_t genesisBlockNum, uint64_t lastReachableBlockNum);
  void checkUnreachableBlocks(uint64_t lastReachableBlockNum, uint64_t lastBlockNum);
  void checkBlocksBeingFetchedNow(bool checkAllBlocks, uint64_t lastReachableBlockNum, uint64_t lastBlockNum);
  void checkStoredCheckpoints(uint64_t firstStoredCheckpoint, uint64_t lastStoredCheckpoint);

 public:
  ///////////////////////////////////////////////////////////////////////////
  // Compute digests
  ///////////////////////////////////////////////////////////////////////////

  static void computeDigestOfPage(
      const uint32_t pageId, const uint64_t checkpointNumber, const char* page, uint32_t pageSize, STDigest& outDigest);

  static void computeDigestOfPagesDescriptor(const DataStore::ResPagesDescriptor* pagesDesc, STDigest& outDigest);

  static void computeDigestOfBlock(const uint64_t blockNum,
                                   const char* block,
                                   const uint32_t blockSize,
                                   STDigest* outDigest);

  static std::array<std::uint8_t, BLOCK_DIGEST_SIZE> computeDigestOfBlock(const uint64_t blockNum,
                                                                          const char* block,
                                                                          const uint32_t blockSize);

  // A wrapper function to get a block from the IAppState and compute its digest.
  //
  // SIDE EFFECT: This function mutates buffer_ and resets it to 0 after the fact.
  STDigest getBlockAndComputeDigest(uint64_t currBlock);

  ///////////////////////////////////////////////////////////////////////////
  // Metrics
  ///////////////////////////////////////////////////////////////////////////
 public:
  void SetAggregator(std::shared_ptr<concordMetrics::Aggregator> a);

 private:
  void loadMetrics();
  std::chrono::seconds last_metrics_dump_time_;
  std::chrono::seconds metrics_dump_interval_in_sec_;
  concordMetrics::Component metrics_component_;
  struct Metrics {
    StatusHandle fetching_state_;
    StatusHandle preferred_replicas_;

    GaugeHandle current_source_replica_;
    GaugeHandle checkpoint_being_fetched_;
    GaugeHandle last_stored_checkpoint_;
    GaugeHandle number_of_reserved_pages_;
    GaugeHandle size_of_reserved_page_;
    GaugeHandle last_msg_seq_num_;
    GaugeHandle next_required_block_;
    GaugeHandle num_pending_item_data_msgs_;
    GaugeHandle total_size_of_pending_item_data_msgs_;
    GaugeHandle last_block_;
    GaugeHandle last_reachable_block_;

    CounterHandle sent_ask_for_checkpoint_summaries_msg_;
    CounterHandle sent_checkpoint_summary_msg_;

    CounterHandle sent_fetch_blocks_msg_;
    CounterHandle sent_fetch_res_pages_msg_;
    CounterHandle sent_reject_fetch_msg_;
    CounterHandle sent_item_data_msg_;

    CounterHandle received_ask_for_checkpoint_summaries_msg_;
    CounterHandle received_checkpoint_summary_msg_;
    CounterHandle received_fetch_blocks_msg_;
    CounterHandle received_fetch_res_pages_msg_;
    CounterHandle received_reject_fetching_msg_;
    CounterHandle received_item_data_msg_;
    CounterHandle received_illegal_msg_;

    CounterHandle invalid_ask_for_checkpoint_summaries_msg_;
    CounterHandle irrelevant_ask_for_checkpoint_summaries_msg_;
    CounterHandle invalid_checkpoint_summary_msg_;
    CounterHandle irrelevant_checkpoint_summary_msg_;
    CounterHandle invalid_fetch_blocks_msg_;
    CounterHandle irrelevant_fetch_blocks_msg_;
    CounterHandle invalid_fetch_res_pages_msg_;
    CounterHandle irrelevant_fetch_res_pages_msg_;
    CounterHandle invalid_reject_fetching_msg_;
    CounterHandle irrelevant_reject_fetching_msg_;
    CounterHandle invalid_item_data_msg_;
    CounterHandle irrelevant_item_data_msg_;

    CounterHandle create_checkpoint_;
    CounterHandle mark_checkpoint_as_stable_;
    CounterHandle load_reserved_page_;
    CounterHandle load_reserved_page_from_pending_;
    CounterHandle load_reserved_page_from_checkpoint_;
    CounterHandle save_reserved_page_;
    CounterHandle zero_reserved_page_;
    CounterHandle start_collecting_state_;
    CounterHandle on_timer_;

    CounterHandle on_transferring_complete_;

    GaugeHandle overall_blocks_collected_;
    GaugeHandle overall_blocks_throughtput_;
    GaugeHandle overall_bytes_collected_;
    GaugeHandle overall_bytes_throughtput_;
    GaugeHandle prev_win_blocks_collected_;
    GaugeHandle prev_win_blocks_throughtput_;
    GaugeHandle prev_win_bytes_collected_;
    GaugeHandle prev_win_bytes_throughtput_;
  };

  mutable Metrics metrics_;

  concord::util::CallbackRegistry<uint64_t> on_transferring_complete_cb_registry_;

  ///////////////////////////////////////////////////////////////////////////
  // Internal Statistics
  ///////////////////////////////////////////////////////////////////////////
 protected:
  static constexpr uint32_t get_missing_blocks_summary_window_size = checkpointWindowSize;
  Throughput blocks_collected_;
  Throughput bytes_collected_;
  std::optional<uint64_t> first_collected_block_num_;

  // used to print periodic summary of recent checkpoints, and collected date while in state GettingMissingBlocks
  std::string logsForCollectingStatus(const uint64_t firstRequiredBlock);
  void reportCollectingStatus(const uint64_t firstRequiredBlock, const uint32_t actualBlockSize);
  void startCollectingStats();

  ///////////////////////////////////////////////////////////////////////////
  // Latency Historgrams
  ///////////////////////////////////////////////////////////////////////////
 private:
  static constexpr int64_t MAX_VALUE_MILLISECONDS = 1000 * 60;  // 60 Seconds
  static constexpr int64_t MAX_VALUE_MICROSECONDS = 1000 * 1000 * 60;
  using Recorder = concord::diagnostics::Recorder;

  struct Recorders {
    Recorders() {
      auto& registrar = concord::diagnostics::RegistrarSingleton::getInstance();
      registrar.perf.registerComponent("state_transfer", {fetch_blocks_msg_latency, on_timer});
    }
    DEFINE_SHARED_RECORDER(
        fetch_blocks_msg_latency, 1, MAX_VALUE_MILLISECONDS, 3, concord::diagnostics::Unit::MILLISECONDS);
    DEFINE_SHARED_RECORDER(on_timer, 1, MAX_VALUE_MICROSECONDS, 3, concord::diagnostics::Unit::MICROSECONDS);
  };
  Recorders histograms_;

  // Record latency for FetchBlockMsg <-> ItemDataMsg. These messages are sent multiple times between dest and src
  concord::diagnostics::AsyncTimeRecorderMap<SeqNum, true> fetch_block_msg_latency_rec_;

};  // namespace bftEngine::bcst::impl

}  // namespace bftEngine::bcst::impl
