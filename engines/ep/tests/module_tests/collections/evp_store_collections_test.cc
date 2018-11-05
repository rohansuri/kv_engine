/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

/**
 * Tests for Collection functionality in EPStore.
 */
#include "bgfetcher.h"
#include "checkpoint_manager.h"
#include "collections/collections_types.h"
#include "ep_time.h"
#include "kvstore.h"
#include "programs/engine_testapp/mock_server.h"
#include "tests/mock/mock_global_task.h"
#include "tests/mock/mock_synchronous_ep_engine.h"
#include "tests/module_tests/collections/test_manifest.h"
#include "tests/module_tests/evp_store_single_threaded_test.h"
#include "tests/module_tests/evp_store_test.h"
#include "tests/module_tests/test_helpers.h"

#include <functional>
#include <thread>

class CollectionsTest : public SingleThreadedKVBucketTest {
public:
    void SetUp() override {
        // Enable collections (which will enable namespace persistence).
        config_string += "collections_enabled=true";
        SingleThreadedKVBucketTest::SetUp();
        // Start vbucket as active to allow us to store items directly to it.
        store->setVBucketState(vbid, vbucket_state_active, false);
    }

    Collections::VB::PersistedManifest getManifest(Vbid vb) const {
        return store->getVBucket(vb)
                ->getShard()
                ->getRWUnderlying()
                ->getCollectionsManifest(vbid);
    }
};

TEST_F(CollectionsTest, uid_increment) {
    CollectionsManifest cm{CollectionEntry::meat};
    EXPECT_EQ(store->setCollections({cm}).code(), cb::engine_errc::success);
    cm.add(CollectionEntry::vegetable);
    EXPECT_EQ(store->setCollections({cm}).code(), cb::engine_errc::success);
}

TEST_F(CollectionsTest, uid_decrement) {
    CollectionsManifest cm{CollectionEntry::meat};
    EXPECT_EQ(store->setCollections({cm}).code(), cb::engine_errc::success);
    CollectionsManifest newCm{};
    EXPECT_EQ(store->setCollections({newCm}).code(),
              cb::engine_errc::out_of_range);
}

TEST_F(CollectionsTest, uid_equal) {
    CollectionsManifest cm{CollectionEntry::meat};
    EXPECT_EQ(store->setCollections({cm}).code(), cb::engine_errc::success);

    // Test we return out_of_range if manifest uid is same
    EXPECT_EQ(store->setCollections({cm}).code(),
              cb::engine_errc::out_of_range);
}

// This test stores a key which matches what collections internally uses, but
// in a different namespace.
TEST_F(CollectionsTest, namespace_separation) {
    // Use the event factory to get an event which we'll borrow the key from
    auto se = SystemEventFactory::make(SystemEvent::Collection, "meat", {}, {});
    DocKey key(se->getKey().data(),
               se->getKey().size(),
               DocKeyEncodesCollectionId::No);

    store_item(vbid, key, "value");
    VBucketPtr vb = store->getVBucket(vbid);
    // Add the meat collection
    CollectionsManifest cm(CollectionEntry::meat);
    vb->updateFromManifest({cm});
    // Trigger a flush to disk. Flushes the meat create event and 1 item
    flush_vbucket_to_disk(vbid, 2);

    // evict and load - should not see the system key for create collections
    evict_key(vbid, key);
    get_options_t options = static_cast<get_options_t>(
            QUEUE_BG_FETCH | HONOR_STATES | TRACK_REFERENCE | DELETE_TEMP |
            HIDE_LOCKED_CAS | TRACK_STATISTICS);
    GetValue gv = store->get(key, vbid, cookie, options);
    EXPECT_EQ(ENGINE_EWOULDBLOCK, gv.getStatus());

    // Manually run the BGFetcher task; to fetch the two outstanding
    // requests (for the same key).
    runBGFetcherTask();

    gv = store->get(key, vbid, cookie, options);
    EXPECT_EQ(ENGINE_SUCCESS, gv.getStatus());
    EXPECT_EQ(0, strncmp("value", gv.item->getData(), gv.item->getNBytes()));
}

TEST_F(CollectionsTest, collections_basic) {
    // Default collection is open for business
    store_item(vbid, StoredDocKey{"key", CollectionEntry::defaultC}, "value");
    store_item(vbid,
               StoredDocKey{"meat:beef", CollectionEntry::meat},
               "value",
               0,
               {cb::engine_errc::unknown_collection});

    VBucketPtr vb = store->getVBucket(vbid);

    // Add the meat collection
    CollectionsManifest cm(CollectionEntry::meat);
    vb->updateFromManifest({cm});

    // Trigger a flush to disk. Flushes the meat create event and 1 item
    flush_vbucket_to_disk(vbid, 2);

    // Now we can write to beef
    store_item(vbid, StoredDocKey{"meat:beef", CollectionEntry::meat}, "value");

    flush_vbucket_to_disk(vbid, 1);

    // And read a document from beef
    get_options_t options = static_cast<get_options_t>(
            QUEUE_BG_FETCH | HONOR_STATES | TRACK_REFERENCE | DELETE_TEMP |
            HIDE_LOCKED_CAS | TRACK_STATISTICS);

    GetValue gv = store->get(StoredDocKey{"meat:beef", CollectionEntry::meat},
                             vbid,
                             cookie,
                             options);
    ASSERT_EQ(ENGINE_SUCCESS, gv.getStatus());

    // A key in meat that doesn't exist
    gv = store->get(StoredDocKey{"meat:sausage", CollectionEntry::meat},
                    vbid,
                    cookie,
                    options);
    EXPECT_EQ(ENGINE_KEY_ENOENT, gv.getStatus());

    // Begin the deletion
    vb->updateFromManifest({cm.remove(CollectionEntry::meat)});

    // We should have deleted the create marker
    flush_vbucket_to_disk(vbid, 1);

    // Access denied (although the item still exists)
    gv = store->get(StoredDocKey{"meat:beef", CollectionEntry::meat},
                    vbid,
                    cookie,
                    options);
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION, gv.getStatus());
}

// BY-ID update: This test was created for MB-25344 and is no longer relevant as
// we cannot 'hit' a logically deleted key from the front-end. This test has
// been adjusted to still provide some value.
TEST_F(CollectionsTest, unknown_collection_errors) {
    VBucketPtr vb = store->getVBucket(vbid);
    // Add the dairy collection
    CollectionsManifest cm(CollectionEntry::dairy);
    vb->updateFromManifest({cm});
    // Trigger a flush to disk. Flushes the dairy create event.
    flush_vbucket_to_disk(vbid, 1);

    auto item1 = make_item(vbid,
                           StoredDocKey{"dairy:milk", CollectionEntry::dairy},
                           "creamy",
                           0,
                           0);
    EXPECT_EQ(ENGINE_SUCCESS, store->add(item1, cookie));
    flush_vbucket_to_disk(vbid, 1);

    auto item2 = make_item(vbid,
                           StoredDocKey{"dairy:cream", CollectionEntry::dairy},
                           "creamy",
                           0,
                           0);
    EXPECT_EQ(ENGINE_SUCCESS, store->add(item2, cookie));
    flush_vbucket_to_disk(vbid, 1);

    // Delete the dairy collection (so all dairy keys become logically deleted)
    vb->updateFromManifest({cm.remove(CollectionEntry::dairy)});

    // Re-add the dairy collection
    vb->updateFromManifest({cm.add(CollectionEntry::dairy2)});

    // Trigger a flush to disk. Flushes the dairy2 create event, dairy delete.
    flush_vbucket_to_disk(vbid, 2);

    // Expect that we cannot add item1 again, item1 has no collection
    item1.setCas(0);
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION, store->add(item1, cookie));

    // Replace should fail, item2 has no collection
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION, store->replace(item2, cookie));

    // Delete should fail, item2 has no collection
    uint64_t cas = 0;
    mutation_descr_t mutation_descr;
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->deleteItem(item2.getKey(),
                                cas,
                                vbid,
                                cookie,
                                nullptr,
                                mutation_descr));

    // Unlock should fail 'unknown-col' rather than an unlock error
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->unlockKey(item2.getKey(), vbid, 0, ep_current_time()));

    EXPECT_EQ("collection_unknown",
              store->validateKey(
                      StoredDocKey{"meat:sausage", CollectionEntry::meat},
                      vbid,
                      item2));
    EXPECT_EQ("collection_unknown",
              store->validateKey(item2.getKey(), vbid, item2));

    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->statsVKey(
                      StoredDocKey{"meat:sausage", CollectionEntry::meat},
                      vbid,
                      cookie));
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->statsVKey(item2.getKey(), vbid, cookie));

    // GetKeyStats
    struct key_stats ks;
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->getKeyStats(
                      item2.getKey(), vbid, cookie, ks, WantsDeleted::No));
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->getKeyStats(
                      item2.getKey(), vbid, cookie, ks, WantsDeleted::Yes));

    uint32_t deleted = 0;
    uint8_t dtype = 0;
    ItemMetaData meta;
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->getMetaData(
                      item2.getKey(), vbid, nullptr, meta, deleted, dtype));

    cas = 0;
    meta.cas = 1;
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->deleteWithMeta(item2.getKey(),
                                    cas,
                                    nullptr,
                                    vbid,
                                    nullptr,
                                    {vbucket_state_active},
                                    CheckConflicts::No,
                                    meta,
                                    false,
                                    GenerateBySeqno::Yes,
                                    GenerateCas::No,
                                    0,
                                    nullptr,
                                    false));

    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
              store->setWithMeta(item2,
                                 0,
                                 nullptr,
                                 nullptr,
                                 {vbucket_state_active},
                                 CheckConflicts::Yes,
                                 false,
                                 GenerateBySeqno::Yes,
                                 GenerateCas::No));

    const char* msg = nullptr;
    EXPECT_EQ(cb::mcbp::Status::UnknownCollection,
              store->evictKey(item2.getKey(), vbid, &msg));
}

// BY-ID update: This test was created for MB-25344 and is no longer relevant as
// we cannot 'hit' a logically deleted key from the front-end. This test has
// been adjusted to still provide some value.
TEST_F(CollectionsTest, GET_unknown_collection_errors) {
    VBucketPtr vb = store->getVBucket(vbid);
    // Add the dairy collection
    CollectionsManifest cm(CollectionEntry::dairy);
    vb->updateFromManifest({cm});
    // Trigger a flush to disk. Flushes the dairy create event.
    flush_vbucket_to_disk(vbid, 1);

    auto item1 = make_item(vbid,
                           StoredDocKey{"dairy:milk", CollectionEntry::dairy},
                           "creamy",
                           0,
                           0);
    EXPECT_EQ(ENGINE_SUCCESS, store->add(item1, cookie));
    flush_vbucket_to_disk(vbid, 1);

    // Delete the dairy collection (so all dairy keys become logically deleted)
    vb->updateFromManifest({cm.remove(CollectionEntry::dairy)});

    // Re-add the dairy collection
    vb->updateFromManifest({cm.add(CollectionEntry::dairy2)});

    // Trigger a flush to disk. Flushes the dairy2 create event, dairy delete
    flush_vbucket_to_disk(vbid, 2);

    // The dairy:2 collection is empty
    get_options_t options = static_cast<get_options_t>(
            QUEUE_BG_FETCH | HONOR_STATES | TRACK_REFERENCE | DELETE_TEMP |
            HIDE_LOCKED_CAS | TRACK_STATISTICS | GET_DELETED_VALUE);

    // Get deleted can't get it
    auto gv = store->get(StoredDocKey{"dairy:milk", CollectionEntry::dairy},
                         vbid,
                         cookie,
                         options);
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION, gv.getStatus());

    options = static_cast<get_options_t>(QUEUE_BG_FETCH | HONOR_STATES |
                                         TRACK_REFERENCE | DELETE_TEMP |
                                         HIDE_LOCKED_CAS | TRACK_STATISTICS);

    // Normal Get can't get it
    gv = store->get(StoredDocKey{"dairy:milk", CollectionEntry::dairy},
                    vbid,
                    cookie,
                    options);
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION, gv.getStatus());

    // Same for getLocked
    gv = store->getLocked(StoredDocKey{"dairy:milk", CollectionEntry::dairy},
                          vbid,
                          ep_current_time(),
                          10,
                          cookie);
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION, gv.getStatus());

    // Same for getAndUpdateTtl
    gv = store->getAndUpdateTtl(
            StoredDocKey{"dairy:milk", CollectionEntry::dairy},
            vbid,
            cookie,
            ep_current_time() + 20);
    EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION, gv.getStatus());
}

class CollectionsFlushTest : public CollectionsTest {
public:
    void SetUp() override {
        CollectionsTest::SetUp();
    }

    void collectionsFlusher(int items);

private:
    Collections::VB::PersistedManifest createCollectionAndFlush(
            const std::string& json, CollectionID collection, int items);
    Collections::VB::PersistedManifest deleteCollectionAndFlush(
            const std::string& json, CollectionID collection, int items);
    Collections::VB::PersistedManifest completeDeletionAndFlush(
            CollectionID collection, int items);

    void storeItems(CollectionID collection,
                    int items,
                    cb::engine_errc = cb::engine_errc::success);

    /**
     * Create manifest object from persisted manifest and validate if we can
     * write to the collection.
     * @param data - The persisted manifest data
     * @param collection - a collection name to test for writing
     *
     * @return true if the collection can be written
     */
    static bool canWrite(const Collections::VB::PersistedManifest& data,
                         CollectionID collection);

    /**
     * Create manifest object from persisted manifest and validate if we can
     * write to the collection.
     * @param data - The persisted manifest data
     * @param collection - a collection name to test for writing
     *
     * @return true if the collection cannot be written
     */
    static bool cannotWrite(const Collections::VB::PersistedManifest& data,
                            CollectionID collection);
};

void CollectionsFlushTest::storeItems(CollectionID collection,
                                      int items,
                                      cb::engine_errc expected) {
    for (int ii = 0; ii < items; ii++) {
        std::string key = "key" + std::to_string(ii);
        store_item(vbid, StoredDocKey{key, collection}, "value", 0, {expected});
    }
}

Collections::VB::PersistedManifest
CollectionsFlushTest::createCollectionAndFlush(const std::string& json,
                                               CollectionID collection,
                                               int items) {
    VBucketPtr vb = store->getVBucket(vbid);
    // cannot write to collection
    storeItems(collection, items, cb::engine_errc::unknown_collection);
    vb->updateFromManifest(json);
    storeItems(collection, items);
    flush_vbucket_to_disk(vbid, 1 + items); // create event + items
    EXPECT_EQ(items, vb->lockCollections().getItemCount(collection));
    return getManifest(vbid);
}

Collections::VB::PersistedManifest
CollectionsFlushTest::deleteCollectionAndFlush(const std::string& json,
                                               CollectionID collection,
                                               int items) {
    VBucketPtr vb = store->getVBucket(vbid);
    storeItems(collection, items);
    vb->updateFromManifest(json);
    // cannot write to collection
    storeItems(collection, items, cb::engine_errc::unknown_collection);
    flush_vbucket_to_disk(vbid, 1 + items); // 1x del(create event) + items
    // Eraser yet to run, so count will still show items
    EXPECT_EQ(items, vb->lockCollections().getItemCount(collection));
    return getManifest(vbid);
}

Collections::VB::PersistedManifest
CollectionsFlushTest::completeDeletionAndFlush(CollectionID collection,
                                               int items) {
    // complete deletion by triggering the erase of collection (Which calls
    // completed once it's purged all items of the deleted collection)
    runCompaction();

    // Default is still ok
    storeItems(CollectionID::Default, items);
    flush_vbucket_to_disk(vbid, items); // just the items

    // No item count check here, the call will throw for the deleted collection
    return getManifest(vbid);
}

bool CollectionsFlushTest::canWrite(
        const Collections::VB::PersistedManifest& data,
        CollectionID collection) {
    Collections::VB::Manifest manifest(data);
    std::string key = std::to_string(collection);
    return manifest.lock().doesKeyContainValidCollection(
            StoredDocKey{key, collection});
}

bool CollectionsFlushTest::cannotWrite(
        const Collections::VB::PersistedManifest& data,
        CollectionID collection) {
    return !canWrite(data, collection);
}

/**
 * Drive manifest state changes through the test's vbucket
 *  1. Validate the flusher flushes the expected items
 *  2. Validate the updated collections manifest changes
 *  3. Use a validator function to check if a collection is (or is not)
 *     writeable
 */
void CollectionsFlushTest::collectionsFlusher(int items) {
    struct testFuctions {
        std::function<Collections::VB::PersistedManifest()> function;
        std::function<bool(const Collections::VB::PersistedManifest&)>
                validator;
    };

    CollectionsManifest cm(CollectionEntry::meat);
    using std::placeholders::_1;
    // Setup the test using a vector of functions to run
    std::vector<testFuctions> test{
            // First 3 steps - add,delete,complete for the meat collection
            {// 0
             std::bind(&CollectionsFlushTest::createCollectionAndFlush,
                       this,
                       cm,
                       CollectionEntry::meat,
                       items),
             std::bind(&CollectionsFlushTest::canWrite,
                       _1,
                       CollectionEntry::meat)},

            {// 1
             std::bind(&CollectionsFlushTest::deleteCollectionAndFlush,
                       this,
                       cm.remove(CollectionEntry::meat),
                       CollectionEntry::meat,
                       items),
             std::bind(&CollectionsFlushTest::cannotWrite,
                       _1,
                       CollectionEntry::meat)},
            {// 2
             std::bind(&CollectionsFlushTest::completeDeletionAndFlush,
                       this,
                       CollectionEntry::meat,
                       items),
             std::bind(&CollectionsFlushTest::cannotWrite,
                       _1,
                       CollectionEntry::meat)},

            // Final 4 steps - add,delete,add,complete for the fruit collection
            {// 3
             std::bind(&CollectionsFlushTest::createCollectionAndFlush,
                       this,
                       cm.add(CollectionEntry::dairy),
                       CollectionEntry::dairy,
                       items),
             std::bind(&CollectionsFlushTest::canWrite,
                       _1,
                       CollectionEntry::dairy)},
            {// 4
             std::bind(&CollectionsFlushTest::deleteCollectionAndFlush,
                       this,
                       cm.remove(CollectionEntry::dairy),
                       CollectionEntry::dairy,
                       items),
             std::bind(&CollectionsFlushTest::cannotWrite,
                       _1,
                       CollectionEntry::dairy)},
            {// 5
             std::bind(&CollectionsFlushTest::createCollectionAndFlush,
                       this,
                       cm.add(CollectionEntry::dairy2),
                       CollectionEntry::dairy2,
                       items),
             std::bind(&CollectionsFlushTest::canWrite,
                       _1,
                       CollectionEntry::dairy2)},
            {// 6
             std::bind(&CollectionsFlushTest::completeDeletionAndFlush,
                       this,
                       CollectionEntry::dairy,
                       items),
             std::bind(&CollectionsFlushTest::canWrite,
                       _1,
                       CollectionEntry::dairy2)}};

    Collections::VB::PersistedManifest m1;
    int step = 0;
    for (auto& f : test) {
        auto m2 = f.function();
        // The manifest should change for each step
        EXPECT_NE(m1, m2) << "Failed step:" + std::to_string(step) << "\n"
                          << m1 << "\n should not match " << m2 << "\n";
        EXPECT_TRUE(f.validator(m2))
                << "Failed at step:" << std::to_string(step) << " validating "
                << m2;
        m1 = m2;
        step++;
    }
}

TEST_F(CollectionsFlushTest, collections_flusher_no_items) {
    collectionsFlusher(0);
}

TEST_F(CollectionsFlushTest, collections_flusher_with_items) {
    collectionsFlusher(3);
}

class CollectionsWarmupTest : public SingleThreadedKVBucketTest {
public:
    void SetUp() override {
        // Enable collections (which will enable namespace persistence).
        config_string += "collections_enabled=true";
        SingleThreadedKVBucketTest::SetUp();
        setVBucketStateAndRunPersistTask(vbid, vbucket_state_active);
    }
};

// Test item counting when we store/delete flush and store again
TEST_F(CollectionsTest, MB_31212) {
    CollectionsManifest cm;
    auto vb = store->getVBucket(vbid);

    vb->updateFromManifest({cm.add(CollectionEntry::meat)});
    auto key = StoredDocKey{"beef", CollectionEntry::meat};
    // Now we can write to meat
    store_item(vbid, key, "value");
    delete_item(vbid, key);

    // Trigger a flush to disk. Flushes the meat create event and the delete
    flush_vbucket_to_disk(vbid, 2);

    // 0 items, we only have a delete on disk
    EXPECT_EQ(0, vb->lockCollections().getItemCount(CollectionEntry::meat));

    // Store the same key again and expect 1 item
    store_item(vbid, StoredDocKey{"beef", CollectionEntry::meat}, "value");

    flush_vbucket_to_disk(vbid, 1);
    EXPECT_EQ(1, vb->lockCollections().getItemCount(CollectionEntry::meat));
}

//
// Create a collection then create a second engine which will warmup from the
// persisted collection state and should have the collection accessible.
//
TEST_F(CollectionsWarmupTest, warmup) {
    CollectionsManifest cm;
    cm.setUid(0xface2);
    {
        auto vb = store->getVBucket(vbid);

        vb->updateFromManifest({cm.add(CollectionEntry::meat)});

        // Trigger a flush to disk. Flushes the meat create event
        flush_vbucket_to_disk(vbid, 1);

        // Now we can write to beef
        store_item(vbid,
                   StoredDocKey{"meat:beef", CollectionEntry::meat},
                   "value");
        // But not dairy
        store_item(vbid,
                   StoredDocKey{"dairy:milk", CollectionEntry::dairy},
                   "value",
                   0,
                   {cb::engine_errc::unknown_collection});

        flush_vbucket_to_disk(vbid, 1);

        EXPECT_EQ(1, vb->lockCollections().getItemCount(CollectionEntry::meat));
    } // VBucketPtr scope ends

    resetEngineAndWarmup();

    // validate the manifest uid comes back
    EXPECT_EQ(0xface2,
              store->getVBucket(vbid)->lockCollections().getManifestUid());

    // validate we warmup the item count
    EXPECT_EQ(1,
              store->getVBucket(vbid)->lockCollections().getItemCount(
                      CollectionEntry::meat));

    {
        Item item(StoredDocKey{"meat:beef", CollectionEntry::meat},
                  /*flags*/ 0,
                  /*exp*/ 0,
                  "rare",
                  sizeof("rare"));
        item.setVBucketId(vbid);
        uint64_t cas;
        EXPECT_EQ(ENGINE_SUCCESS,
                  engine->storeInner(cookie, &item, cas, OPERATION_SET));
    }
    {
        Item item(StoredDocKey{"dairy:milk", CollectionEntry::dairy},
                  /*flags*/ 0,
                  /*exp*/ 0,
                  "skimmed",
                  sizeof("skimmed"));
        item.setVBucketId(vbid);
        uint64_t cas;
        EXPECT_EQ(ENGINE_UNKNOWN_COLLECTION,
                  engine->storeInner(cookie, &item, cas, OPERATION_SET));
    }

    EXPECT_EQ(1,
              store->getVBucket(vbid)->lockCollections().getItemCount(
                      CollectionEntry::meat));
}

// When a collection is deleted - an event enters the checkpoint which does not
// enter the persisted seqno index - hence at the end of this test when we warm
// up, expect the highSeqno to be less than before the warmup.
TEST_F(CollectionsWarmupTest, MB_25381) {
    int64_t highSeqno = 0;
    {
        auto vb = store->getVBucket(vbid);

        // Add the dairy collection
        CollectionsManifest cm(CollectionEntry::dairy);
        vb->updateFromManifest({cm});

        // Trigger a flush to disk. Flushes the dairy create event
        flush_vbucket_to_disk(vbid, 1);

        // Now we can write to dairy
        store_item(vbid,
                   StoredDocKey{"dairy:milk", CollectionEntry::dairy},
                   "creamy");

        // Now delete the dairy collection
        vb->updateFromManifest({cm.remove(CollectionEntry::dairy)
                                        .add(CollectionEntry::fruit)});

        flush_vbucket_to_disk(vbid, 3);

        EXPECT_EQ(1,
                  vb->lockCollections().getItemCount(CollectionEntry::dairy));

        // This pushes an Item which doesn't flush but has consumed a seqno
        vb->completeDeletion(CollectionEntry::dairy);

        flush_vbucket_to_disk(vbid, 0); // 0 items but has written _local

        highSeqno = vb->getHighSeqno();
    } // VBucketPtr scope ends
    resetEngineAndWarmup();

    auto vb = store->getVBucket(vbid);
    EXPECT_GT(highSeqno, vb->getHighSeqno());
}

//
// Create a collection then create a second engine which will warmup from the
// persisted collection state and should have the collection accessible.
//
TEST_F(CollectionsWarmupTest, warmupIgnoreLogicallyDeleted) {
    {
        auto vb = store->getVBucket(vbid);

        // Add the meat collection
        CollectionsManifest cm(CollectionEntry::meat);
        vb->updateFromManifest({cm});

        // Trigger a flush to disk. Flushes the meat create event
        flush_vbucket_to_disk(vbid, 1);
        const int nitems = 10;
        for (int ii = 0; ii < nitems; ii++) {
            // Now we can write to beef
            std::string key = "meat:" + std::to_string(ii);
            store_item(vbid, StoredDocKey{key, CollectionEntry::meat}, "value");
        }

        flush_vbucket_to_disk(vbid, nitems);

        // Remove the meat collection
        vb->updateFromManifest({cm.remove(CollectionEntry::meat)});

        flush_vbucket_to_disk(vbid, 1);

        EXPECT_EQ(nitems, vb->ht.getNumInMemoryItems());
        EXPECT_EQ(nitems,
                  vb->lockCollections().getItemCount(CollectionEntry::meat));
    } // VBucketPtr scope ends

    resetEngineAndWarmup();

    EXPECT_EQ(0, store->getVBucket(vbid)->ht.getNumInMemoryItems());
    // Eraser hasn't ran, but the collection deletion will have resulted in
    // the stat document being removed
    EXPECT_EQ(0,
              store->getVBucket(vbid)->lockCollections().getItemCount(
                      CollectionEntry::meat));
}

//
// Create a collection then create a second engine which will warmup from the
// persisted collection state and should have the collection accessible.
//
TEST_F(CollectionsWarmupTest, warmupIgnoreLogicallyDeletedDefault) {
    {
        auto vb = store->getVBucket(vbid);

        // Add the meat collection
        CollectionsManifest cm(CollectionEntry::meat);
        vb->updateFromManifest({cm});

        // Trigger a flush to disk. Flushes the meat create event
        flush_vbucket_to_disk(vbid, 1);
        const int nitems = 10;
        for (int ii = 0; ii < nitems; ii++) {
            std::string key = "key" + std::to_string(ii);
            store_item(vbid,
                       StoredDocKey{key, CollectionEntry::defaultC},
                       "value");
        }

        flush_vbucket_to_disk(vbid, nitems);

        // Remove the default collection
        vb->updateFromManifest({cm.remove(CollectionEntry::defaultC)});

        flush_vbucket_to_disk(vbid, 1);

        EXPECT_EQ(nitems, vb->ht.getNumInMemoryItems());
        EXPECT_EQ(
                nitems,
                vb->lockCollections().getItemCount(CollectionEntry::defaultC));
    } // VBucketPtr scope ends

    resetEngineAndWarmup();

    EXPECT_EQ(0, store->getVBucket(vbid)->ht.getNumInMemoryItems());
    // Eraser hasn't ran, but the collection deletion will have resulted in
    // the stat document being removed
    EXPECT_EQ(0,
              store->getVBucket(vbid)->lockCollections().getItemCount(
                      CollectionEntry::defaultC));
}

TEST_F(CollectionsWarmupTest, warmupManifestUidLoadsOnCreate) {
    {
        auto vb = store->getVBucket(vbid);

        // Add the meat collection
        CollectionsManifest cm;
        cm.setUid(0xface2);
        vb->updateFromManifest({cm.add(CollectionEntry::meat)});

        flush_vbucket_to_disk(vbid, 1);
    } // VBucketPtr scope ends

    resetEngineAndWarmup();

    // validate the manifest uid comes back
    EXPECT_EQ(0xface2,
              store->getVBucket(vbid)->lockCollections().getManifestUid());
}

TEST_F(CollectionsWarmupTest, warmupManifestUidLoadsOnDelete) {
    {
        auto vb = store->getVBucket(vbid);

        // Delete the $default collection
        CollectionsManifest cm;
        cm.setUid(0xface2);
        vb->updateFromManifest({cm.remove(CollectionEntry::defaultC)});

        flush_vbucket_to_disk(vbid, 1);
    } // VBucketPtr scope ends

    resetEngineAndWarmup();

    // validate the manifest uid comes back
    EXPECT_EQ(0xface2,
              store->getVBucket(vbid)->lockCollections().getManifestUid());
}

class CollectionsManagerTest : public CollectionsTest {};

/**
 * Test checks that setCollections propagates the collection data to active
 * vbuckets.
 */
TEST_F(CollectionsManagerTest, basic) {
    // Add some more VBuckets just so there's some iteration happening
    const int extraVbuckets = 2;
    for (int vb = vbid.get() + 1; vb <= (vbid.get() + extraVbuckets); vb++) {
        store->setVBucketState(Vbid(vb), vbucket_state_active, false);
    }

    CollectionsManifest cm(CollectionEntry::meat);
    store->setCollections({cm});

    // Check all vbuckets got the collections
    for (int vb = vbid.get(); vb <= (vbid.get() + extraVbuckets); vb++) {
        auto vbp = store->getVBucket(Vbid(vb));
        EXPECT_TRUE(vbp->lockCollections().doesKeyContainValidCollection(
                StoredDocKey{"meat:bacon", CollectionEntry::meat}));
        EXPECT_TRUE(vbp->lockCollections().doesKeyContainValidCollection(
                StoredDocKey{"anykey", CollectionEntry::defaultC}));
    }
}

/**
 * Test checks that setCollections propagates the collection data to active
 * vbuckets and not the replicas
 */
TEST_F(CollectionsManagerTest, basic2) {
    // Add some more VBuckets just so there's some iteration happening
    const int extraVbuckets = 2;
    // Add active and replica
    for (int vb = vbid.get() + 1; vb <= (vbid.get() + extraVbuckets); vb++) {
        if (vb & 1) {
            store->setVBucketState(Vbid(vb), vbucket_state_active, false);
        } else {
            store->setVBucketState(Vbid(vb), vbucket_state_replica, false);
        }
    }

    CollectionsManifest cm(CollectionEntry::meat);
    store->setCollections({cm});

    // Check all vbuckets got the collections
    for (int vb = vbid.get(); vb <= (vbid.get() + extraVbuckets); vb++) {
        auto vbp = store->getVBucket(Vbid(vb));
        if (vbp->getState() == vbucket_state_active) {
            EXPECT_TRUE(vbp->lockCollections().doesKeyContainValidCollection(
                    StoredDocKey{"meat:bacon", CollectionEntry::meat}));
            EXPECT_TRUE(vbp->lockCollections().doesKeyContainValidCollection(
                    StoredDocKey{"anykey", CollectionEntry::defaultC}));
        } else {
            // Replica will be in default constructed settings
            EXPECT_FALSE(vbp->lockCollections().doesKeyContainValidCollection(
                    StoredDocKey{"meat:bacon", CollectionEntry::meat}));
            EXPECT_TRUE(vbp->lockCollections().doesKeyContainValidCollection(
                    StoredDocKey{"anykey", CollectionEntry::defaultC}));
        }
    }
}

/**
 * Add a collection, delete it and add it again (i.e. a CID re-use)
 * We should see a failure
 */
TEST_F(CollectionsManagerTest, cid_clash) {
    // Add some more VBuckets just so there's some iteration happening
    const int extraVbuckets = 2;
    for (int vb = vbid.get() + 1; vb <= (vbid.get() + extraVbuckets); vb++) {
        store->setVBucketState(Vbid(vb), vbucket_state_active, false);
    }

    CollectionsManifest cm;
    EXPECT_EQ(cb::engine_errc::success,
              store->setCollections({cm.add(CollectionEntry::meat)}).code());
    EXPECT_EQ(cb::engine_errc::success,
              store->setCollections({cm.remove(CollectionEntry::meat)}).code());
    EXPECT_EQ(cb::engine_errc::cannot_apply_collections_manifest,
              store->setCollections({cm.add(CollectionEntry::meat)}).code());
}

// Test the compactor doesn't generate expired items for a dropped collection
TEST_F(CollectionsTest, collections_expiry_after_drop_collection_compaction) {
    VBucketPtr vb = store->getVBucket(vbid);

    // Add the meat collection + 1 item with TTL (and flush it all out)
    CollectionsManifest cm(CollectionEntry::meat);
    vb->updateFromManifest({cm});
    StoredDocKey key{"lamb", CollectionEntry::meat};
    store_item(vbid, key, "value", ep_real_time() + 100);
    flush_vbucket_to_disk(vbid, 2);
    // And now drop the meat collection
    vb->updateFromManifest({cm.remove(CollectionEntry::meat)});
    flush_vbucket_to_disk(vbid, 1);

    // Time travel
    TimeTraveller docBrown(2000);

    // Now compact to force expiry of our little lamb
    runCompaction();

    std::vector<queued_item> items;
    vb->checkpointManager->getAllItemsForPersistence(items);

    // No mutation of the original key is allowed as it would invalidate the
    // ordering of create @x, item @y, drop @z  x < y < z
    for (auto& i : items) {
        EXPECT_NE(key, i->getKey());
    }
}

// Test the pager doesn't generate expired items for a dropped collection
TEST_F(CollectionsTest, collections_expiry_after_drop_collection_pager) {
    VBucketPtr vb = store->getVBucket(vbid);

    // Add the meat collection + 1 item with TTL (and flush it all out)
    CollectionsManifest cm(CollectionEntry::meat);
    vb->updateFromManifest({cm});
    StoredDocKey key{"lamb", CollectionEntry::meat};
    store_item(vbid, key, "value", ep_real_time() + 100);
    flush_vbucket_to_disk(vbid, 2);
    // And now drop the meat collection
    vb->updateFromManifest({cm.remove(CollectionEntry::meat)});
    flush_vbucket_to_disk(vbid, 1);

    // Time travel
    TimeTraveller docBrown(2000);

    // Now run the pager to force expiry of our little lamb
    auto task = std::make_shared<ExpiredItemPager>(
            engine.get(), engine->getEpStats(), 0);
    static_cast<ExpiredItemPager*>(task.get())->run();
    runNextTask(*task_executor->getLpTaskQ()[NONIO_TASK_IDX],
                "Expired item remover on vb:0");

    std::vector<queued_item> items;
    vb->checkpointManager->getAllItemsForPersistence(items);

    // No mutation of the original key is allowed as it would invalidate the
    // ordering of create @x, item @y, drop @z  x < y < z
    for (auto& i : items) {
        EXPECT_NE(key, i->getKey());
    }
}
