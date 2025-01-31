/**
 *    Copyright (C) 2024-present MongoDB, Inc.
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

#include <map>
#include <scoped_allocator>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/db/timeseries/timeseries_tracked_types.h"
#include "mongo/db/timeseries/timeseries_tracking_allocator.h"
#include "mongo/unittest/unittest.h"

namespace mongo {

class TrackingAllocatorTest : public unittest::Test {};

TEST_F(TrackingAllocatorTest, STLContainerSimple) {
    timeseries::TrackingContext trackingContext;
    ASSERT_EQ(0, trackingContext.allocated());

    {
        std::vector<int64_t, timeseries::TrackingAllocator<int64_t>> vec(
            trackingContext.makeAllocator<int64_t>());
        ASSERT_EQ(0, trackingContext.allocated());

        vec.push_back(1);
        ASSERT_EQ(sizeof(int64_t), trackingContext.allocated());

        vec.push_back(2);
        vec.push_back(3);

        // Different compilers allocate capacity in different ways. Use shrink_to_fit() to reduce
        // capacity to the size.
        vec.shrink_to_fit();
        ASSERT_EQ(3 * sizeof(int64_t), trackingContext.allocated());

        vec.pop_back();
        vec.shrink_to_fit();
        ASSERT_EQ(2 * sizeof(int64_t), trackingContext.allocated());

        vec.clear();
        vec.shrink_to_fit();
        ASSERT_EQ(0, trackingContext.allocated());
    }

    ASSERT_EQ(0, trackingContext.allocated());
}

TEST_F(TrackingAllocatorTest, STLContainerCopy) {
    timeseries::TrackingContext trackingContext;
    ASSERT_EQ(0, trackingContext.allocated());

    {
        std::vector<int64_t, timeseries::TrackingAllocator<int64_t>> vec(
            trackingContext.makeAllocator<int64_t>());
        ASSERT_EQ(0, trackingContext.allocated());

        vec.push_back(1);
        ASSERT_EQ(sizeof(int64_t), trackingContext.allocated());

        std::vector<int64_t, timeseries::TrackingAllocator<int64_t>> vecCopy = vec;
        vecCopy.shrink_to_fit();
        ASSERT_EQ(2 * sizeof(int64_t), trackingContext.allocated());

        vecCopy.push_back(2);
        vecCopy.shrink_to_fit();
        ASSERT_EQ(3 * sizeof(int64_t), trackingContext.allocated());
    }

    ASSERT_EQ(0, trackingContext.allocated());
}

TEST_F(TrackingAllocatorTest, STLContainerMove) {
    timeseries::TrackingContext trackingContext;
    ASSERT_EQ(0, trackingContext.allocated());

    {
        std::vector<int64_t, timeseries::TrackingAllocator<int64_t>> vec(
            trackingContext.makeAllocator<int64_t>());
        ASSERT_EQ(0, trackingContext.allocated());

        vec.push_back(1);
        ASSERT_EQ(sizeof(int64_t), trackingContext.allocated());

        std::vector<int64_t, timeseries::TrackingAllocator<int64_t>> vecMove = std::move(vec);
        vecMove.shrink_to_fit();
        ASSERT_EQ(sizeof(int64_t), trackingContext.allocated());

        vecMove.push_back(2);
        vecMove.shrink_to_fit();
        ASSERT_EQ(2 * sizeof(int64_t), trackingContext.allocated());
    }

    ASSERT_EQ(0, trackingContext.allocated());
}

TEST_F(TrackingAllocatorTest, STLContainerNested) {
    timeseries::TrackingContext trackingContext;
    ASSERT_EQ(0, trackingContext.allocated());

    {
        // The scoped_allocator_adaptor is necessary for multilevel containers to use the same
        // allocator.
        using Key = int64_t;
        using Value = std::vector<int64_t, timeseries::TrackingAllocator<int64_t>>;
        timeseries::tracked_map<Key, Value> map(
            timeseries::make_tracked_map<Key, Value>(trackingContext));

        map[1].emplace_back(1);
        ASSERT_GT(trackingContext.allocated(), sizeof(std::pair<const Key, Value>));

        // Adding elements into the vector results in the top-level allocators allocation count
        // being increased.
        uint64_t prevAllocated = trackingContext.allocated();
        for (int i = 0; i < 100; i++) {
            map[1].push_back(i);
        }
        ASSERT_GT(trackingContext.allocated(), prevAllocated);

        prevAllocated = trackingContext.allocated();
        map[1].clear();
        map[1].shrink_to_fit();
        ASSERT_LT(trackingContext.allocated(), prevAllocated);
    }

    ASSERT_EQ(0, trackingContext.allocated());

    {
        using Key = timeseries::tracked_string;
        using Value = std::vector<timeseries::tracked_string>;
        timeseries::tracked_map<Key, Value> map(
            timeseries::make_tracked_map<Key, Value>(trackingContext));

        const timeseries::tracked_string str =
            timeseries::make_tracked_string(trackingContext, "mystring", 9);
        map[str].emplace_back(str);
        ASSERT_GT(trackingContext.allocated(),
                  sizeof(std::pair<const Key, Value>) + 2 * str.capacity());
    }

    ASSERT_EQ(0, trackingContext.allocated());
}

TEST_F(TrackingAllocatorTest, ManagedObject) {
    class MockClass {
    private:
        int64_t u;
        int64_t v;
        int64_t w;
        int64_t x;
        int64_t y;
        int64_t z;
    };

    timeseries::TrackingContext trackingContext;
    ASSERT_EQ(0, trackingContext.allocated());

    {
        timeseries::shared_tracked_ptr<MockClass> mockClass =
            timeseries::make_shared_tracked<MockClass>(trackingContext);
        ASSERT_GTE(trackingContext.allocated(), sizeof(MockClass));
    }

    ASSERT_EQ(0, trackingContext.allocated());

    {
        timeseries::unique_tracked_ptr<MockClass> mockClass =
            timeseries::make_unique_tracked<MockClass>(trackingContext);
        ASSERT_GTE(trackingContext.allocated(), sizeof(MockClass));
    }

    ASSERT_EQ(0, trackingContext.allocated());
}

}  // namespace mongo
