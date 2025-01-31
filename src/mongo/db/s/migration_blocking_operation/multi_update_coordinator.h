/**
 *    Copyright (C) 2023-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/db/repl/primary_only_service.h"
#include "mongo/db/s/migration_blocking_operation/multi_update_coordinator.h"
#include "mongo/db/s/migration_blocking_operation/multi_update_coordinator_gen.h"
#include "mongo/db/s/primary_only_service_helpers/retry_until_majority_commit.h"

namespace mongo {

class MultiUpdateCoordinatorExternalState {
public:
    virtual Future<DbResponse> sendClusterUpdateCommandToShards(OperationContext* opCtx,
                                                                const Message& message) const = 0;
    virtual void startBlockingMigrations() const = 0;
    virtual void stopBlockingMigrations() const = 0;
    virtual ~MultiUpdateCoordinatorExternalState() {}
};

class MultiUpdateCoordinatorExternalStateImpl : public MultiUpdateCoordinatorExternalState {
public:
    Future<DbResponse> sendClusterUpdateCommandToShards(OperationContext* opCtx,
                                                        const Message& message) const override;
    void startBlockingMigrations() const override;
    void stopBlockingMigrations() const override;
};

class MultiUpdateCoordinatorExternalStateFactory {
public:
    virtual std::unique_ptr<MultiUpdateCoordinatorExternalState> createExternalState() const = 0;
    virtual ~MultiUpdateCoordinatorExternalStateFactory() {}
};

class MultiUpdateCoordinatorExternalStateFactoryImpl
    : public MultiUpdateCoordinatorExternalStateFactory {
public:
    std::unique_ptr<MultiUpdateCoordinatorExternalState> createExternalState() const {
        return std::make_unique<MultiUpdateCoordinatorExternalStateImpl>();
    }
};

class MultiUpdateCoordinatorInstance;

class MultiUpdateCoordinatorService : public repl::PrimaryOnlyService {
public:
    static constexpr StringData kServiceName = "MultiUpdateCoordinatorService"_sd;

    friend MultiUpdateCoordinatorInstance;

    MultiUpdateCoordinatorService(
        ServiceContext* serviceContext,
        std::unique_ptr<MultiUpdateCoordinatorExternalStateFactory> factory =
            std::make_unique<MultiUpdateCoordinatorExternalStateFactoryImpl>());

    StringData getServiceName() const override;

    NamespaceString getStateDocumentsNS() const override;

    ThreadPool::Limits getThreadPoolLimits() const override;

    void checkIfConflictsWithOtherInstances(
        OperationContext* opCtx,
        BSONObj initialState,
        const std::vector<const Instance*>& existingInstances) override;

    std::shared_ptr<PrimaryOnlyService::Instance> constructInstance(BSONObj initialState) override;

private:
    ServiceContext* _serviceContext;
    std::unique_ptr<MultiUpdateCoordinatorExternalStateFactory> _externalStateFactory;
};

class MultiUpdateCoordinatorInstance
    : public repl::PrimaryOnlyService::TypedInstance<MultiUpdateCoordinatorInstance> {
public:
    MultiUpdateCoordinatorInstance(const MultiUpdateCoordinatorService* service,
                                   MultiUpdateCoordinatorDocument initialDocument);

    SemiFuture<void> run(std::shared_ptr<executor::ScopedTaskExecutor> executor,
                         const CancellationToken& stepdownToken) noexcept override;

    void interrupt(Status status) override;

    boost::optional<BSONObj> reportForCurrentOp(
        MongoProcessInterface::CurrentOpConnectionsMode connMode,
        MongoProcessInterface::CurrentOpSessionsMode sessionMode) noexcept override;

    void checkIfOptionsConflict(const BSONObj& stateDoc) const override;

    const MultiUpdateCoordinatorMetadata& getMetadata() const;

    SharedSemiFuture<BSONObj> getCompletionFuture() const;

private:
    MultiUpdateCoordinatorMutableFields _getMutableFields() const;
    MultiUpdateCoordinatorStateEnum _getCurrentState() const;
    MultiUpdateCoordinatorDocument _buildCurrentStateDocument() const;

    void _initializeRun(std::shared_ptr<executor::ScopedTaskExecutor> executor,
                        const CancellationToken& stepdownToken);
    ExecutorFuture<void> _transitionToState(MultiUpdateCoordinatorStateEnum newState);

    void _updateInMemoryState(const MultiUpdateCoordinatorDocument& newStateDocument);
    void _updateOnDiskState(OperationContext* opCtx,
                            const MultiUpdateCoordinatorDocument& newStateDocument);

    ExecutorFuture<void> _startBlockingMigrations();
    ExecutorFuture<void> _performUpdate();
    ExecutorFuture<void> _checkForPendingUpdates();
    ExecutorFuture<void> _cleanup();
    void _stopBlockingMigrations();

    const MultiUpdateCoordinatorService* const _service;

    mutable Mutex _mutex = MONGO_MAKE_LATCH("MultiUpdateCoordinatorInstance::_mutex");
    const MultiUpdateCoordinatorMetadata _metadata;
    MultiUpdateCoordinatorMutableFields _mutableFields;
    std::unique_ptr<MultiUpdateCoordinatorExternalState> _externalState;

    std::shared_ptr<executor::ScopedTaskExecutor> _taskExecutor;
    boost::optional<primary_only_service_helpers::CancelState> _cancelState;
    boost::optional<primary_only_service_helpers::RetryUntilMajorityCommit> _retry;

    SharedPromise<BSONObj> _completionPromise;
    boost::optional<BSONObj> _cmdResponse;
};

}  // namespace mongo
