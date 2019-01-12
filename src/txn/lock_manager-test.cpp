#include <stdio.h>

#include "lock_manager.h"

#include <chrono>
#include <random>
#include <thread>

#include <gtest/gtest.h>

std::vector<lock_request_t> make_lock_requests(std::vector<std::pair<const char *, bool>> data) {
        std::vector<lock_request_t> res;
        for (const auto &pair : data) {
                lock_request_t lock_request;
                lock_request.path = (char*) pair.first;
                lock_request.write_lock = pair.second;
                res.push_back(lock_request);
        }
        return res;
}

TEST(LockManagerTest, ReadWrite) {
        lock_manager_t *lm = new_lock_manager();

        auto test_read = make_lock_requests({{"test", false}});
        auto test_write = make_lock_requests({{"test", true}});

        lock_handle_t *lh = lm_try_lock(lm, test_read.data(), test_read.size());
        ASSERT_TRUE(lh);

        ASSERT_FALSE(lm_try_lock(lm, test_write.data(), test_write.size()));

        unlock_handle(lh);

        lh = lm_try_lock(lm, test_write.data(), test_write.size());
        ASSERT_TRUE(lh);
        unlock_handle(lh);

        free_lock_manager(lm);
}


TEST(LockManagerTest, ReadRead) {
        lock_manager_t *lm = new_lock_manager();

        auto test_read = make_lock_requests({{"test", false}});
        auto test_write = make_lock_requests({{"test", true}});

        lock_handle_t *lh = lm_try_lock(lm, test_read.data(), test_read.size());
        ASSERT_TRUE(lh);

        lock_handle_t *lh2 = lm_try_lock(lm, test_read.data(), test_read.size());
        ASSERT_TRUE(lh2);

        unlock_handle(lh);

        ASSERT_FALSE(lm_try_lock(lm, test_write.data(), test_write.size()));

        unlock_handle(lh2);


        lh = lm_try_lock(lm, test_write.data(), test_write.size());
        ASSERT_TRUE(lh);
        unlock_handle(lh);

        free_lock_manager(lm);
}

TEST(LockManagerTest, WriteWrite) {
        lock_manager_t *lm = new_lock_manager();

        auto test_write = make_lock_requests({{"test", true}});

        lock_handle_t *lh = lm_try_lock(lm, test_write.data(), test_write.size());
        ASSERT_TRUE(lh);

        ASSERT_FALSE(lm_try_lock(lm, test_write.data(), test_write.size()));

        unlock_handle(lh);

        lh = lm_try_lock(lm, test_write.data(), test_write.size());
        ASSERT_TRUE(lh);
        unlock_handle(lh);

        free_lock_manager(lm);
}

TEST(LockManagerTest, WriteRead) {
        lock_manager_t *lm = new_lock_manager();

        auto test_read = make_lock_requests({{"test", false}});
        auto test_write = make_lock_requests({{"test", true}});

        lock_handle_t *lh = lm_try_lock(lm, test_write.data(), test_write.size());
        ASSERT_TRUE(lh);

        ASSERT_FALSE(lm_try_lock(lm, test_read.data(), test_read.size()));

        unlock_handle(lh);

        lh = lm_try_lock(lm, test_write.data(), test_write.size());
        ASSERT_TRUE(lh);
        unlock_handle(lh);

        free_lock_manager(lm);
}

TEST(LockManagerTest, SharedParentWriteTest) {
        auto test_a = make_lock_requests({{"test/a", true}});
        auto test_b = make_lock_requests({{"test/b", true}});

        lock_manager_t *lm = new_lock_manager();

        lock_handle_t *lh = lm_try_lock(lm, test_a.data(), test_a.size());
        ASSERT_TRUE(lh);

        lock_handle_t *lh2 = lm_try_lock(lm, test_b.data(), test_b.size());
        ASSERT_TRUE(lh2);

        unlock_handle(lh);
        unlock_handle(lh2);

        free_lock_manager(lm);
}

TEST(LockManagerTest, DeepThreadTest) {
        lock_manager_t *lm = new_lock_manager();

        std::vector<std::thread> threads;
        std::string path;
        for (char c : std::string("abcdef")) {
                path += std::string("/") + c;
                threads.push_back(std::thread([&lm, path]() {
                        std::uniform_int_distribution<int> rand(1, 5);
                        std::default_random_engine rng;
                        for (int i = 0; i < 100; i++) {
                                auto lr = make_lock_requests({{path.c_str(), true}});
                                lock_handle_t *lh2 = lm_lock(lm, lr.data(), lr.size());
                                ASSERT_TRUE(lh2);
                                std::this_thread::sleep_for(
                                    std::chrono::milliseconds(rand(rng)));
                                unlock_handle(lh2);
                        }
                }));
        }

        for (auto &thread : threads) {
                thread.join();
        }

        free_lock_manager(lm);
}

TEST(LockManagerTest, ManyThreadTest) {
        lock_manager_t *lm = new_lock_manager();

        auto lr = make_lock_requests({{"test", true}});

        std::vector<std::thread> threads;
        for (int t = 0; t < 10; t++) {
                threads.push_back(std::thread([&lm, &lr]() {
                        std::uniform_int_distribution<int> rand(1, 5);
                        std::default_random_engine rng;
                        for (int i = 0; i < 100; i++) {
                                lock_handle_t *lh2 = lm_lock(lm, lr.data(), lr.size());
                                ASSERT_TRUE(lh2);
                                std::this_thread::sleep_for(
                                    std::chrono::milliseconds(rand(rng)));
                                unlock_handle(lh2);
                        }
                }));
        }

        for (auto &thread : threads) {
                thread.join();
        }

        free_lock_manager(lm);
}

TEST(LockManagerTest, ThreadTest) {
        lock_manager_t *lm = new_lock_manager();

        auto lr = make_lock_requests({{"test", true}});

        lock_handle_t *lh = lm_try_lock(lm, lr.data(), lr.size());
        ASSERT_TRUE(lh);

        std::thread thread([&lm, &lr]() {
                // Ensure file starts locked
                ASSERT_FALSE(lm_try_lock(lm, lr.data(), lr.size()));
                lock_handle_t *lh2 = lm_lock(lm, lr.data(), lr.size());
                ASSERT_TRUE(lh2);
                unlock_handle(lh2);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        unlock_handle(lh);

        thread.join();

        free_lock_manager(lm);
}

TEST(LockManagerTest, LockSets) {
        lock_manager_t *lm = new_lock_manager();

        auto lr = make_lock_requests({{"test", false}, {"test/a", false}, {"test/b", false}, {"test/b/c", false}, {"test/c", true}});
        lock_handle_t *lh = lm_try_lock(lm, lr.data(), lr.size());
        ASSERT_TRUE(lh);

        auto lr2 = make_lock_requests({{"test/a", false}, {"test/b", false}});
        lock_handle_t *lh2 = lm_try_lock(lm, lr2.data(), lr2.size());
        ASSERT_TRUE(lh2);

        unlock_handle(lh);

        auto lr3 = make_lock_requests({{"test", false}, {"test/b/c", false}, {"test/c", true}});
        lock_handle_t *lh3 = lm_try_lock(lm, lr3.data(), lr3.size());
        ASSERT_TRUE(lh3);

        unlock_handle(lh3);

        auto lr4 = make_lock_requests({{"test/a", true}});
        ASSERT_FALSE(lm_try_lock(lm, lr4.data(), lr4.size()));
        auto lr5 = make_lock_requests({{"test/b", true}});
        ASSERT_FALSE(lm_try_lock(lm, lr5.data(), lr5.size()));

        unlock_handle(lh2);

        auto lr6 = make_lock_requests({{"test", true}, {"test/a", true}, {"test/b", true}, {"test/b/c", true}, {"test/c", true}});
        lock_handle_t *lh6 = lm_try_lock(lm, lr6.data(), lr6.size());
        ASSERT_TRUE(lh6);
        unlock_handle(lh6);

        free_lock_manager(lm);
}

TEST(LockManagerTest, SanitizationTest) {
        lock_manager_t *lm = new_lock_manager();

        auto test_a = make_lock_requests({{"test/a", true}});
        auto test_a2 = make_lock_requests({{"test/b/../a", true}});

        lock_handle_t *lh = lm_try_lock(lm, test_a.data(), test_a.size());
        ASSERT_TRUE(lh);

        ASSERT_FALSE(lm_try_lock(lm, test_a2.data(), test_a2.size()));

        unlock_handle(lh);

        lh = lm_try_lock(lm, test_a.data(), test_a.size());
        ASSERT_TRUE(lh);
        unlock_handle(lh);

        free_lock_manager(lm);
}

TEST(LockManagerTest, LockDuplicateSets) {
        lock_manager_t *lm = new_lock_manager();

        auto lr = make_lock_requests({{"test", false}, {"test", false}});
        lock_handle_t *lh = lm_try_lock(lm, lr.data(), lr.size());
        ASSERT_TRUE(lh);

        auto write_test = make_lock_requests({{"test", true}});
        lock_handle_t *lh2 = lm_try_lock(lm, write_test.data(), write_test.size());
        ASSERT_FALSE(lh2);

        auto read_test = make_lock_requests({{"test", false}});
        lock_handle_t *lh3 = lm_try_lock(lm, read_test.data(), read_test.size());
        ASSERT_TRUE(lh3);
        unlock_handle(lh3);

        unlock_handle(lh);

        lh2 = lm_try_lock(lm, write_test.data(), write_test.size());
        ASSERT_TRUE(lh2);

        unlock_handle(lh2);

        free_lock_manager(lm);
}

// The case where we lock the same file for reading and writing in one request.
// The lock manager will make a write lock if any of the locks given are write
// locks and a read lock otherwise
TEST(LockManagerTest, DuplicateReadAndWriteRequest) {
        lock_manager_t *lm = new_lock_manager();

        auto lr = make_lock_requests({{"test", false}, {"test", false}, {"test", true}, {"test", false}, {"test/c", true}});
        lock_handle_t *lh = lm_try_lock(lm, lr.data(), lr.size());
        ASSERT_TRUE(lh);

        auto write_test = make_lock_requests({{"test", false}});
        lock_handle_t *lh2 = lm_try_lock(lm, write_test.data(), write_test.size());
        ASSERT_FALSE(lh2);

        auto c = make_lock_requests({{"test/c", false}});
        lock_handle_t *lh3 = lm_try_lock(lm, c.data(), c.size());
        ASSERT_FALSE(lh3);

        unlock_handle(lh);

        lh2 = lm_try_lock(lm, write_test.data(), write_test.size());
        ASSERT_TRUE(lh2);

        unlock_handle(lh2);

        lh3 = lm_try_lock(lm, c.data(), c.size());
        ASSERT_TRUE(lh3);

        unlock_handle(lh3);

        free_lock_manager(lm);
}

TEST(LockManagerTest, TrickySanitizedDuplicate) {
        lock_manager_t *lm = new_lock_manager();

        auto lr = make_lock_requests({{"test", false}, {"test", false}, {"/a/../test", true}, {"test", false}, {"test/c", true}});
        lock_handle_t *lh = lm_try_lock(lm, lr.data(), lr.size());
        ASSERT_TRUE(lh);

        auto write_test = make_lock_requests({{"/b/../test", false}});
        lock_handle_t *lh2 = lm_try_lock(lm, write_test.data(), write_test.size());
        ASSERT_FALSE(lh2);

        auto c = make_lock_requests({{"test/c", false}});
        lock_handle_t *lh3 = lm_try_lock(lm, c.data(), c.size());
        ASSERT_FALSE(lh3);

        unlock_handle(lh);

        lh2 = lm_try_lock(lm, write_test.data(), write_test.size());
        ASSERT_TRUE(lh2);

        unlock_handle(lh2);

        lh3 = lm_try_lock(lm, c.data(), c.size());
        ASSERT_TRUE(lh3);

        unlock_handle(lh3);

        free_lock_manager(lm);
}


int main(int argc, char **argv) {
        ::testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
}
