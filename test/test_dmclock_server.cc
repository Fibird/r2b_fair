// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

/*
 * Copyright (C) 2021 Renmin Univeristy of China
 *
 * Author: Chaoyang Liu <lcy96@ruc.edu.cn>
 *
 * Modified from dmclock (https://github.com/ceph/dmclock)
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version
 * 2.1, as published by the Free Software Foundation.  See file
 * COPYING.
 */

/*
 * Copyright (C) 2016 Red Hat Inc.
 *
 * Author: J. Eric Ivancich <ivancich@redhat.com>
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version
 * 2.1, as published by the Free Software Foundation.  See file
 * COPYING.
 */


#include <memory>
#include <chrono>
#include <iostream>
#include <list>
#include <vector>
#include <thread>


#include "dmclock_server.h"
#include "dmclock_util.h"
#include "gtest/gtest.h"

// process control to prevent core dumps during gtest death tests
#include "dmcPrCtl.h"


namespace dmc = crimson::dmclock;

// we need a request object; an empty one will do
struct Request {
};


namespace crimson {
    namespace dmclock {

        /*
         * Allows us to test the code provided with the mutex provided locked.
         */
        static void test_locked(std::mutex &mtx, std::function<void()> code) {
            std::unique_lock<std::mutex> l(mtx);
            code();
        }


        TEST(dmclock_server, bad_tag_deathtest) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 18;

            double reservation = 0.0;
            double weight = 0.0;

            dmc::ClientInfo ci1(reservation, weight, 0.0);
            dmc::ClientInfo ci2(reservation, weight, 1.0);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &ci1;
                else if (client2 == c) return &ci2;
                else {
                    ADD_FAILURE() << "got request from neither of two clients";
                    return nullptr;
                }
            };

            QueueRef pq(new Queue(client_info_f, false));
            ReqParams req_params(1, 1);

            // Disable coredumps
            PrCtl unset_dumpable;

            EXPECT_DEATH_IF_SUPPORTED(pq->add_request(Request{}, client1, req_params),
                                      "Assertion.*reservation.*max_tag.*"
                                      "proportion.*max_tag") <<
                                                             "we should fail if a client tries to generate a reservation tag "
                                                             "where reservation and proportion are both 0";


            EXPECT_DEATH_IF_SUPPORTED(pq->add_request(Request{}, client2, req_params),
                                      "Assertion.*reservation.*max_tag.*"
                                      "proportion.*max_tag") <<
                                                             "we should fail if a client tries to generate a reservation tag "
                                                             "where reservation and proportion are both 0";
        }


        TEST(dmclock_server, client_idle_erase) {
            using ClientId = int;
            using Queue = dmc::PushPriorityQueue<ClientId, Request>;
            int client = 17;
            double reservation = 100.0;

            dmc::ClientInfo ci(reservation, 1.0, 0.0, dmc::ClientType::A);
            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &ci;
            };
            auto server_ready_f = []() -> bool { return true; };
            auto submit_req_f = [](const ClientId &c,
                                   std::unique_ptr<Request> req,
                                   dmc::PhaseType phase) {
                // empty; do nothing
            };

            Queue pq(client_info_f,
                     server_ready_f,
                     submit_req_f,
                     std::chrono::seconds(3),
                     std::chrono::seconds(5),
                     std::chrono::seconds(2),
                     false);

            auto lock_pq = [&](std::function<void()> code) {
                test_locked(pq.data_mtx, code);
            };


            /* The timeline should be as follows:
             *
             *     0 seconds : request created
             *
             *     1 seconds : map is size 1, idle is false
             *
             * 2 seconds : clean notes first mark; +2 is base for further calcs
             *
             * 4 seconds : clean does nothing except makes another mark
             *
             *   5 seconds : when we're secheduled to idle (+2 + 3)
             *
             * 6 seconds : clean idles client
             *
             *   7 seconds : when we're secheduled to erase (+2 + 5)
             *
             *     7 seconds : verified client is idle
             *
             * 8 seconds : clean erases client info
             *
             *     9 seconds : verified client is erased
             */

            lock_pq([&]() {
                EXPECT_EQ(0u, pq.client_map.size()) <<
                                                    "client map initially has size 0";
            });

            Request req;
            dmc::ReqParams req_params(1, 1);
            pq.add_request_time(req, client, req_params, dmc::get_time());

            std::this_thread::sleep_for(std::chrono::seconds(1));

            lock_pq([&]() {
                EXPECT_EQ(1u, pq.client_map.size()) <<
                                                    "client map has 1 after 1 client";
                EXPECT_FALSE(pq.client_map.at(client)->idle) <<
                                                             "initially client map entry shows not idle.";
            });

            std::this_thread::sleep_for(std::chrono::seconds(6));

            lock_pq([&]() {
                EXPECT_TRUE(pq.client_map.at(client)->idle) <<
                                                            "after idle age client map entry shows idle.";
            });

            std::this_thread::sleep_for(std::chrono::seconds(2));

            lock_pq([&]() {
                EXPECT_EQ(0u, pq.client_map.size()) <<
                                                    "client map loses its entry after erase age";
            });
        } // TEST


#if 0
        TEST(dmclock_server, reservation_timing) {
          using ClientId = int;
          // NB? PUSH OR PULL
          using Queue = std::unique_ptr<dmc::PriorityQueue<ClientId,Request>>;
          using std::chrono::steady_clock;

          int client = 17;

          std::vector<dmc::Time> times;
          std::mutex times_mtx;
          using Guard = std::lock_guard<decltype(times_mtx)>;

          // reservation every second
          dmc::ClientInfo ci(1.0, 0.0, 0.0);
          Queue pq;

          auto client_info_f = [&] (ClientId c) -> const dmc::ClientInfo* {
        return &ci;
          };
          auto server_ready_f = [] () -> bool { return true; };
          auto submit_req_f = [&] (const ClientId& c,
                       std::unique_ptr<Request> req,
                       dmc::PhaseType phase) {
        {
          Guard g(times_mtx);
          times.emplace_back(dmc::get_time());
        }
        std::thread complete([&](){ pq->request_completed(); });
        complete.detach();
          };

          // NB? PUSH OR PULL
          pq = Queue(new dmc::PriorityQueue<ClientId,Request>(client_info_f,
                                  server_ready_f,
                                  submit_req_f,
                                  false));

          Request req;
          ReqParams<ClientId> req_params(client, 1, 1);

          for (int i = 0; i < 5; ++i) {
        pq->add_request_time(req, req_params, dmc::get_time());
          }

          {
        Guard g(times_mtx);
        std::this_thread::sleep_for(std::chrono::milliseconds(5500));
        EXPECT_EQ(5, times.size()) <<
          "after 5.5 seconds, we should have 5 requests times at 1 second apart";
          }
        } // TEST
#endif


        TEST(dmclock_server, remove_by_req_filter) {
            struct MyReq {
                int id;

                MyReq(int _id) :
                        id(_id) {
                    // empty
                }
            }; // MyReq

            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, MyReq>;
            using MyReqRef = typename Queue::RequestRef;

            ClientId client1 = 17;
            ClientId client2 = 98;

            dmc::ClientInfo info1(0.0, 1.0, 0.0);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &info1;
            };

            Queue pq(client_info_f, true);

            EXPECT_EQ(0u, pq.client_count());
            EXPECT_EQ(0u, pq.request_count());

            ReqParams req_params(1, 1);

            pq.add_request(MyReq(1), client1, req_params);
            pq.add_request(MyReq(11), client1, req_params);
            pq.add_request(MyReq(2), client2, req_params);
            pq.add_request(MyReq(0), client2, req_params);
            pq.add_request(MyReq(13), client2, req_params);
            pq.add_request(MyReq(2), client2, req_params);
            pq.add_request(MyReq(13), client2, req_params);
            pq.add_request(MyReq(98), client2, req_params);
            pq.add_request(MyReq(44), client1, req_params);

            EXPECT_EQ(2u, pq.client_count());
            EXPECT_EQ(9u, pq.request_count());

            pq.remove_by_req_filter([](MyReqRef &&r) -> bool { return 1 == r->id % 2; });

            EXPECT_EQ(5u, pq.request_count());

            std::list<MyReq> capture;
            pq.remove_by_req_filter(
                    [&capture](MyReqRef &&r) -> bool {
                        if (0 == r->id % 2) {
                            capture.push_front(*r);
                            return true;
                        } else {
                            return false;
                        }
                    },
                    true);

            EXPECT_EQ(0u, pq.request_count());
            EXPECT_EQ(5u, capture.size());
            int total = 0;
            for (auto i : capture) {
                total += i.id;
            }
            EXPECT_EQ(146, total) << " sum of captured items should be 146";
        } // TEST


        TEST(dmclock_server, remove_by_req_filter_ordering_forwards_visit) {
            struct MyReq {
                int id;

                MyReq(int _id) :
                        id(_id) {
                    // empty
                }
            }; // MyReq

            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, MyReq>;
            using MyReqRef = typename Queue::RequestRef;

            ClientId client1 = 17;

            dmc::ClientInfo info1(0.0, 1.0, 0.0);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &info1;
            };

            Queue pq(client_info_f, true);

            EXPECT_EQ(0u, pq.client_count());
            EXPECT_EQ(0u, pq.request_count());

            ReqParams req_params(1, 1);

            pq.add_request(MyReq(1), client1, req_params);
            pq.add_request(MyReq(2), client1, req_params);
            pq.add_request(MyReq(3), client1, req_params);
            pq.add_request(MyReq(4), client1, req_params);
            pq.add_request(MyReq(5), client1, req_params);
            pq.add_request(MyReq(6), client1, req_params);

            EXPECT_EQ(1u, pq.client_count());
            EXPECT_EQ(6u, pq.request_count());

            // remove odd ids in forward order and append to end

            std::vector<MyReq> capture;
            pq.remove_by_req_filter(
                    [&capture](MyReqRef &&r) -> bool {
                        if (1 == r->id % 2) {
                            capture.push_back(*r);
                            return true;
                        } else {
                            return false;
                        }
                    },
                    false);

            EXPECT_EQ(3u, pq.request_count());
            EXPECT_EQ(3u, capture.size());
            EXPECT_EQ(1, capture[0].id) << "items should come out in forward order";
            EXPECT_EQ(3, capture[1].id) << "items should come out in forward order";
            EXPECT_EQ(5, capture[2].id) << "items should come out in forward order";

            // remove even ids in reverse order but insert at front so comes
            // out forwards

            std::vector<MyReq> capture2;
            pq.remove_by_req_filter(
                    [&capture2](MyReqRef &&r) -> bool {
                        if (0 == r->id % 2) {
                            capture2.insert(capture2.begin(), *r);
                            return true;
                        } else {
                            return false;
                        }
                    },
                    false);

            EXPECT_EQ(0u, pq.request_count());
            EXPECT_EQ(3u, capture2.size());
            EXPECT_EQ(6, capture2[0].id) << "items should come out in reverse order";
            EXPECT_EQ(4, capture2[1].id) << "items should come out in reverse order";
            EXPECT_EQ(2, capture2[2].id) << "items should come out in reverse order";
        } // TEST


        TEST(dmclock_server, remove_by_req_filter_ordering_backwards_visit) {
            struct MyReq {
                int id;

                MyReq(int _id) :
                        id(_id) {
                    // empty
                }
            }; // MyReq

            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, MyReq>;
            using MyReqRef = typename Queue::RequestRef;

            ClientId client1 = 17;

            dmc::ClientInfo info1(0.0, 1.0, 0.0);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &info1;
            };

            Queue pq(client_info_f, true);

            EXPECT_EQ(0u, pq.client_count());
            EXPECT_EQ(0u, pq.request_count());

            ReqParams req_params(1, 1);

            pq.add_request(MyReq(1), client1, req_params);
            pq.add_request(MyReq(2), client1, req_params);
            pq.add_request(MyReq(3), client1, req_params);
            pq.add_request(MyReq(4), client1, req_params);
            pq.add_request(MyReq(5), client1, req_params);
            pq.add_request(MyReq(6), client1, req_params);

            EXPECT_EQ(1u, pq.client_count());
            EXPECT_EQ(6u, pq.request_count());

            // now remove odd ids in forward order

            std::vector<MyReq> capture;
            pq.remove_by_req_filter(
                    [&capture](MyReqRef &&r) -> bool {
                        if (1 == r->id % 2) {
                            capture.insert(capture.begin(), *r);
                            return true;
                        } else {
                            return false;
                        }
                    },
                    true);

            EXPECT_EQ(3u, pq.request_count());
            EXPECT_EQ(3u, capture.size());
            EXPECT_EQ(1, capture[0].id) << "items should come out in forward order";
            EXPECT_EQ(3, capture[1].id) << "items should come out in forward order";
            EXPECT_EQ(5, capture[2].id) << "items should come out in forward order";

            // now remove even ids in reverse order

            std::vector<MyReq> capture2;
            pq.remove_by_req_filter(
                    [&capture2](MyReqRef &&r) -> bool {
                        if (0 == r->id % 2) {
                            capture2.push_back(*r);
                            return true;
                        } else {
                            return false;
                        }
                    },
                    true);

            EXPECT_EQ(0u, pq.request_count());
            EXPECT_EQ(3u, capture2.size());
            EXPECT_EQ(6, capture2[0].id) << "items should come out in reverse order";
            EXPECT_EQ(4, capture2[1].id) << "items should come out in reverse order";
            EXPECT_EQ(2, capture2[2].id) << "items should come out in reverse order";
        } // TEST


        TEST(dmclock_server, remove_by_client) {
            struct MyReq {
                int id;

                MyReq(int _id) :
                        id(_id) {
                    // empty
                }
            }; // MyReq

            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, MyReq>;
            using MyReqRef = typename Queue::RequestRef;

            ClientId client1 = 17;
            ClientId client2 = 98;

            dmc::ClientInfo info1(0.0, 1.0, 0.0);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &info1;
            };

            Queue pq(client_info_f, true);

            EXPECT_EQ(0u, pq.client_count());
            EXPECT_EQ(0u, pq.request_count());

            ReqParams req_params(1, 1);

            pq.add_request(MyReq(1), client1, req_params);
            pq.add_request(MyReq(11), client1, req_params);
            pq.add_request(MyReq(2), client2, req_params);
            pq.add_request(MyReq(0), client2, req_params);
            pq.add_request(MyReq(13), client2, req_params);
            pq.add_request(MyReq(2), client2, req_params);
            pq.add_request(MyReq(13), client2, req_params);
            pq.add_request(MyReq(98), client2, req_params);
            pq.add_request(MyReq(44), client1, req_params);

            EXPECT_EQ(2u, pq.client_count());
            EXPECT_EQ(9u, pq.request_count());

            std::list<MyReq> removed;

            pq.remove_by_client(client1,
                                true,
                                [&removed](MyReqRef &&r) {
                                    removed.push_front(*r);
                                });

            EXPECT_EQ(3u, removed.size());
            EXPECT_EQ(1, removed.front().id);
            removed.pop_front();
            EXPECT_EQ(11, removed.front().id);
            removed.pop_front();
            EXPECT_EQ(44, removed.front().id);
            removed.pop_front();

            EXPECT_EQ(6u, pq.request_count());

            Queue::PullReq pr = pq.pull_request();
            EXPECT_TRUE(pr.is_retn());
            EXPECT_EQ(2, pr.get_retn().request->id);

            pr = pq.pull_request();
            EXPECT_TRUE(pr.is_retn());
            EXPECT_EQ(0, pr.get_retn().request->id);

            pq.remove_by_client(client2);
            EXPECT_EQ(0u, pq.request_count()) <<
                                              "after second client removed, none left";
        } // TEST


        TEST(dmclock_server_pull, pull_weight) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;

            //for (int ctype = 0; ctype <=2; ctype++) {

            dmc::ClientInfo info1(0.0, 1.0, 0.0, dmc::ClientType::A);
            dmc::ClientInfo info2(0.0, 2.0, 0.0, dmc::ClientType::A);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, false));

            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                now += 0.0001;
            }

            int c1_count = 0;
            int c2_count = 0;
            for (int i = 0; i < 6; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) ++c1_count;
                else if (client2 == retn.client) ++c2_count;
                else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
            }

            EXPECT_EQ(2, c1_count) <<
                                   "one-third of request should have come from first client";
            EXPECT_EQ(4, c2_count) <<
                                   "two-thirds of request should have come from second client";
            //  }
        }


        TEST(dmclock_server_pull, pull_reservation) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 52;
            ClientId client2 = 8;

            dmc::ClientInfo info1(2.0, 0.0, 0.0, dmc::ClientType::R);
            dmc::ClientInfo info2(1.0, 0.0, 0.0, dmc::ClientType::R);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            QueueRef pq(new Queue(client_info_f, false));

            ReqParams req_params(1, 1);

            // make sure all times are well before now
            auto old_time = dmc::get_time() - 100.0;

            for (int i = 0; i < 5; ++i) {
                pq->add_request_time(Request{}, client1, req_params, old_time);
                pq->add_request_time(Request{}, client2, req_params, old_time);
                old_time += 0.001;
            }

            int c1_count = 0;
            int c2_count = 0;

            for (int i = 0; i < 6; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) ++c1_count;
                else if (client2 == retn.client) ++c2_count;
                else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::reservation, retn.phase);
            }

            EXPECT_EQ(4, c1_count) <<
                                   "two-thirds of request should have come from first client";
            EXPECT_EQ(2, c2_count) <<
                                   "one-third of request should have come from second client";
        } // dmclock_server_pull.pull_reservation

        TEST(dmclock_server_pull, pull_burst) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;

            dmc::ClientInfo info1(0.0, 1.0, 0.0, dmc::ClientType::B);
            dmc::ClientInfo info2(0.0, 2.0, 0.0, dmc::ClientType::B);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, 9, 1, false));

            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                now += 0.0001;
            }

            int c1_count = 0;
            int c2_count = 0;
            for (int i = 0; i < 6; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) ++c1_count;
                else if (client2 == retn.client) ++c2_count;
                else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
            }

            EXPECT_EQ(2, c1_count) <<
                                   "one-third of request should have come from first client";
            EXPECT_EQ(4, c2_count) <<
                                   "two-thirds of request should have come from second client";
        }

        TEST(dmclock_server_pull, pull_deltar) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;

            dmc::ClientInfo info1(0.0, 1.0, 0.0, dmc::ClientType::R);
            dmc::ClientInfo info2(0.0, 2.0, 0.0, dmc::ClientType::R);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, 10, 1, false));

            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                now += 0.0001;
            }

            int c1_count = 0;
            int c2_count = 0;
            for (int i = 0; i < 6; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) ++c1_count;
                else if (client2 == retn.client) ++c2_count;
                else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            EXPECT_EQ(2, c1_count) <<
                                   "one-third of request should have come from first client";
            EXPECT_EQ(4, c2_count) <<
                                   "two-thirds of request should have come from second client";
        }

        TEST(dmclock_server_pull, burst_duration) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;

            dmc::ClientInfo info1(0.0, 1.0, 10.0, dmc::ClientType::B);
            dmc::ClientInfo info2(0.0, 3.0, 5.0, dmc::ClientType::B);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, 10, 2, false));

            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                now += 0.0001;
            }

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
            }

            EXPECT_EQ(5, pq->client_map[client1]->resource);
            EXPECT_EQ(15, pq->client_map[client2]->resource);

            int c1_count = 0;
            int c2_count = 0;
            for (int i = 0; i < 10; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) {
                    ++c1_count;
                    EXPECT_EQ(c1_count - 1, pq->client_map[client1]->b_counter);
                } else if (client2 == retn.client) {
                    ++c2_count;
                    EXPECT_EQ(c2_count, pq->client_map[client2]->b_counter);
                } else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            EXPECT_EQ(5, c1_count) <<
                                   "one-third of request should have come from first client";
            EXPECT_EQ(5, c2_count) <<
                                   "two-thirds of request should have come from second client";

            Queue::PullReq pr = pq->pull_request();
            EXPECT_EQ(5, pq->client_map[client1]->b_counter);
            EXPECT_TRUE(now - pq->win_start < pq->win_size);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            pr = pq->pull_request();
            EXPECT_EQ(Queue::NextReqType::future, pr.type);
        }

        TEST(dmclock_server_pull, pull_best_effort) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;
            ClientId client3 = 32;

            dmc::ClientInfo info1(0.0, 1.0, 10.0, dmc::ClientType::B);
            dmc::ClientInfo info2(0.0, 2.0, 5.0, dmc::ClientType::B);
            dmc::ClientInfo info3(0.0, 1.0, 0.0, dmc::ClientType::A);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else if (client3 == c) return &info3;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, 10, 2, false));

            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                pq->add_request(Request{}, client3, req_params);
                now += 0.0001;
            }

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
            }

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client3, req_params);
            }

            int c1_count = 0;
            int c2_count = 0;
            for (int i = 0; i < 10; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) {
                    ++c1_count;
                } else if (client2 == retn.client) {
                    ++c2_count;
                } else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            EXPECT_EQ(5, c1_count);
            EXPECT_EQ(5, c2_count);

            Queue::PullReq pr = pq->pull_request();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            for (int i = 0; i < 4; i++) {
                pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);
                EXPECT_EQ(client3, retn.client);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        TEST(dmclock_server_pull, update_client_info) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request, false>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;

            dmc::ClientInfo info1(0.0, 100.0, 0.0, dmc::ClientType::A);
            dmc::ClientInfo info2(0.0, 200.0, 0.0, dmc::ClientType::A);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, false));

            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                now += 0.0001;
            }

            int c1_count = 0;
            int c2_count = 0;
            for (int i = 0; i < 10; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (i > 5) continue;
                if (client1 == retn.client) ++c1_count;
                else if (client2 == retn.client) ++c2_count;
                else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
            }

            EXPECT_EQ(2, c1_count) <<
                                   "before: one-third of request should have come from first client";
            EXPECT_EQ(4, c2_count) <<
                                   "before: two-thirds of request should have come from second client";

            std::chrono::seconds dura(1);
            std::this_thread::sleep_for(dura);

            info1 = dmc::ClientInfo(0.0, 200.0, 0.0, dmc::ClientType::A);
            pq->update_client_info(17);

            now = dmc::get_time();

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                now += 0.0001;
            }

            c1_count = 0;
            c2_count = 0;
            for (int i = 0; i < 6; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) ++c1_count;
                else if (client2 == retn.client) ++c2_count;
                else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
            }

            EXPECT_EQ(3, c1_count) <<
                                   "after: one-third of request should have come from first client";
            EXPECT_EQ(3, c2_count) <<
                                   "after: two-thirds of request should have come from second client";
        }

        TEST(dmclock_server_pull, schedule_order) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request, false>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;
            ClientId client3 = 35;

            dmc::ClientInfo info1(3.0, 1.0, 0.0, dmc::ClientType::R);
            dmc::ClientInfo info2(0.0, 1.0, 20.0, dmc::ClientType::B);
            dmc::ClientInfo info3(0.0, 1.0, 0.0, dmc::ClientType::A);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else if (client3 == c) return &info3;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };
            pq = QueueRef(new Queue(client_info_f, 15, 1, false));

            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            for (int i = 0; i < 15; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                pq->add_request(Request{}, client3, req_params);
                now += 0.0001;
            }

            int c1_count = 0;
            int c2_count = 0;
            int c3_count = 0;
            int res_count = 0;
            for (int i = 0; i < 15; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) {
                    ++c1_count;
                    if (retn.phase == PhaseType::reservation) res_count++;
                } else if (client2 == retn.client) {
                    EXPECT_EQ(PhaseType::priority, retn.phase);
                    ++c2_count;
                } else if (client3 == retn.client) {
//                    EXPECT_EQ(5, c1_count);
                    EXPECT_EQ(5, c2_count);
                    EXPECT_EQ(PhaseType::priority, retn.phase);
                    ++c3_count;
                } else
                    ADD_FAILURE() << "got request from neither of two clients";

                std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 15));
            }

            EXPECT_EQ(5, c1_count);
            EXPECT_EQ(3, res_count);
            EXPECT_EQ(5, c2_count);
            EXPECT_EQ(5, c3_count);
        }

        TEST(dmclock_server_pull, dynamic_cli_info_f) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request, true>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;

            std::vector<dmc::ClientInfo> info1;
            std::vector<dmc::ClientInfo> info2;

            info1.push_back(dmc::ClientInfo(0.0, 100.0, 0.0, dmc::A));
            info1.push_back(dmc::ClientInfo(0.0, 150.0, 0.0, dmc::A));

            info2.push_back(dmc::ClientInfo(0.0, 200.0, 0.0, dmc::A));
            info2.push_back(dmc::ClientInfo(0.0, 50.0, 0.0, dmc::A));

            uint cli_info_group = 0;

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1[cli_info_group];
                else if (client2 == c) return &info2[cli_info_group];
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, false));

            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            for (int i = 0; i < 5; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                now += 0.0001;
            }

            int c1_count = 0;
            int c2_count = 0;
            for (int i = 0; i < 10; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (i > 5) continue;
                if (client1 == retn.client) ++c1_count;
                else if (client2 == retn.client) ++c2_count;
                else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
            }

            EXPECT_EQ(2, c1_count) <<
                                   "before: one-third of request should have come from first client";
            EXPECT_EQ(4, c2_count) <<
                                   "before: two-thirds of request should have come from second client";

            std::chrono::seconds dura(1);
            std::this_thread::sleep_for(dura);

            cli_info_group = 1;

            now = dmc::get_time();

            for (int i = 0; i < 6; ++i) {
                pq->add_request(Request{}, client1, req_params);
                pq->add_request(Request{}, client2, req_params);
                now += 0.0001;
            }

            c1_count = 0;
            c2_count = 0;
            for (int i = 0; i < 8; ++i) {
                Queue::PullReq pr = pq->pull_request();
                EXPECT_EQ(Queue::NextReqType::returning, pr.type);
                auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);

                if (client1 == retn.client) ++c1_count;
                else if (client2 == retn.client) ++c2_count;
                else
                    ADD_FAILURE() << "got request from neither of two clients";

                EXPECT_EQ(PhaseType::priority, retn.phase);
            }

            EXPECT_EQ(6, c1_count) <<
                                   "after: one-third of request should have come from first client";
            EXPECT_EQ(2, c2_count) <<
                                   "after: two-thirds of request should have come from second client";
        }


        // This test shows what happens when a request can be ready (under
        // limit) but not schedulable since proportion tag is 0. We expect
        // to get some future and none responses.
        TEST(dmclock_server_pull, ready_and_under_limit) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 52;
            ClientId client2 = 8;

            dmc::ClientInfo info1(1.0, 1.0, 0.0, dmc::R);
            dmc::ClientInfo info2(1.0, 1.0, 0.0, dmc::R);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;
                else if (client2 == c) return &info2;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            QueueRef pq = QueueRef(new Queue(client_info_f, 2, 30, false));

            ReqParams req_params(1, 1);

            // make sure all times are well before now
            auto start_time = dmc::get_time() - 100.0;

            // add six requests; for same client reservations spaced one apart
            for (int i = 0; i < 3; ++i) {
                pq->add_request_time(Request{}, client1, req_params, start_time);
                pq->add_request_time(Request{}, client2, req_params, start_time);
            }

//      EXPECT_EQ(0, pq->client_map[client1]->deltar);
            EXPECT_EQ(0, pq->client_map[client1]->deltar);
            Queue::PullReq pr = pq->pull_request(start_time + 0.5);
            EXPECT_EQ(Queue::NextReqType::returning, pr.type);

            pr = pq->pull_request(start_time + 0.5);
            EXPECT_EQ(Queue::NextReqType::returning, pr.type);
            pr = pq->pull_request(start_time + 0.5);
            EXPECT_EQ(Queue::NextReqType::future, pr.type) <<
                                                           "too soon for next reservation";
            pr = pq->pull_request(start_time + 1.5);
            EXPECT_EQ(Queue::NextReqType::returning, pr.type);
            pr = pq->pull_request(start_time + 1.5);
            EXPECT_EQ(Queue::NextReqType::returning, pr.type);
            pr = pq->pull_request(start_time + 1.5);
            EXPECT_EQ(Queue::NextReqType::future, pr.type) <<
                                                           "too soon for next reservation";

            pr = pq->pull_request(start_time + 2.5);
            EXPECT_EQ(Queue::NextReqType::returning, pr.type);

            pr = pq->pull_request(start_time + 2.5);
            EXPECT_EQ(Queue::NextReqType::returning, pr.type);

            pr = pq->pull_request(start_time + 2.5);
            EXPECT_EQ(Queue::NextReqType::none, pr.type) << "no more requests left";
        }


        TEST(dmclock_server_pull, pull_none) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            dmc::ClientInfo info(1.0, 1.0, 1.0);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &info;
            };

            QueueRef pq(new Queue(client_info_f, false));

            // Request req;
            ReqParams req_params(1, 1);

            auto now = dmc::get_time();

            Queue::PullReq pr = pq->pull_request(now + 100);

            EXPECT_EQ(Queue::NextReqType::none, pr.type);
        }


        TEST(dmclock_server_pull, pull_future) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 52;
            // ClientId client2 = 8;

            dmc::ClientInfo info(1.0, 0.0, 0.0, dmc::R);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &info;
            };

            QueueRef pq = QueueRef(new Queue(client_info_f, 1, 30, false));

            ReqParams req_params(1, 1);

            // make sure all times are well before now
            auto now = dmc::get_time();

            pq->add_request_time(Request{}, client1, req_params, now + 100);
            Queue::PullReq pr = pq->pull_request(now);

            EXPECT_EQ(Queue::NextReqType::future, pr.type);

            Time when = boost::get<Time>(pr.data);
            std::cout << "now + 100: " << now + 100 << std::endl;
            std::cout << "when: " << when << std::endl;
            EXPECT_EQ(now + 100, when);
            //EXPECT_DOUBLE_EQ(now + 100, when);
        }


        TEST(dmclock_server_pull, pull_future_limit_break_weight) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 52;
            // ClientId client2 = 8;

            dmc::ClientInfo info(0.0, 1.0, 1.0, dmc::A);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &info;
            };

            QueueRef pq(new Queue(client_info_f, true));

            ReqParams req_params(1, 1);

            // make sure all times are well before now
            auto now = dmc::get_time();

            pq->add_request_time(Request{}, client1, req_params, now + 100);
            Queue::PullReq pr = pq->pull_request(now);

            EXPECT_EQ(Queue::NextReqType::returning, pr.type);

            auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);
            EXPECT_EQ(client1, retn.client);
        }


        TEST(dmclock_server_pull, pull_future_limit_break_reservation) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 52;
            // ClientId client2 = 8;

            dmc::ClientInfo info(1.0, 0.0, 1.0, dmc::R);

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                return &info;
            };

            QueueRef pq(new Queue(client_info_f, true));

            ReqParams req_params(1, 1);

            // make sure all times are well before now
            auto now = dmc::get_time();

            pq->add_request_time(Request{}, client1, req_params, now + 100);
            Queue::PullReq pr = pq->pull_request(now);

            EXPECT_EQ(Queue::NextReqType::returning, pr.type);

            auto &retn = boost::get<Queue::PullReq::Retn>(pr.data);
            EXPECT_EQ(client1, retn.client);
        }

        TEST(dmclock_server, client_resource_update) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request, false>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;
            ClientId client3 = 32;

            dmc::ClientInfo info1(0.0, 100.0, 0.0, dmc::ClientType::A);
            dmc::ClientInfo info2(0.0, 200.0, 0.0, dmc::ClientType::A);
            dmc::ClientInfo info3(0.0, 300.0, 0.0, dmc::ClientType::A);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;//return &info1;
                else if (client2 == c) return &info2;
                else if (client3 == c) return &info3;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, 90, 30, false));

            ReqParams req_params(1, 1);

            pq->add_request(Request{}, client1, req_params);
            EXPECT_EQ(2700, pq->client_map[client1]->resource) <<
                                                               "after: first client's resource is equal system capacity";

            pq->add_request(Request{}, client2, req_params);
            EXPECT_EQ(900, pq->client_map[client1]->resource) <<
                                                              "after: 1st client's resource is updated by weight";
            EXPECT_EQ(1800, pq->client_map[client2]->resource) <<
                                                               "after: 2nd client's resource is updated by weight";
            pq->add_request(Request{}, client3, req_params);
            EXPECT_EQ(450, pq->client_map[client1]->resource) <<
                                                              "after: 1st client's resource is updated by weight";
            EXPECT_EQ(900, pq->client_map[client2]->resource) <<
                                                              "after: 2nd client's resource is updated by weight";
            EXPECT_EQ(1350, pq->client_map[client3]->resource) <<
                                                               "after: 3rd client's resource is updated by weight";

            pq->remove_by_client(client3);
            EXPECT_EQ(900, pq->client_map[client1]->resource) <<
                                                              "after: 1st client's resource is updated by weight";
            EXPECT_EQ(1800, pq->client_map[client2]->resource) <<
                                                               "after: 2nd client's resource is updated by weight";
        }

        TEST(dmclock_server, reserv_client_info) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request, false>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;
            ClientId client3 = 32;

            dmc::ClientInfo info1(100, 1.0, 0.0, dmc::ClientType::R);
            dmc::ClientInfo info2(200, 1.0, 0.0, dmc::ClientType::R);
            dmc::ClientInfo info3(300, 1.0, 0.0, dmc::ClientType::R);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;//return &info1;
                else if (client2 == c) return &info2;
                else if (client3 == c) return &info3;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, 900, 30, false));
            ReqParams req_params(1, 1);

            pq->add_request(Request{}, client1, req_params);
            EXPECT_EQ(800, pq->client_map[client1]->deltar);
            EXPECT_EQ(0, pq->client_info_wrapper(*pq->client_map[client1])->limit);
            EXPECT_EQ(800, pq->client_info_wrapper(*pq->client_map[client1])->weight);

            pq->add_request(Request{}, client2, req_params);

            EXPECT_EQ(350, pq->client_map[client1]->deltar);
//            EXPECT_EQ(250, pq->client_map[client2]->deltar);
            EXPECT_EQ(250, pq->client_map[client2]->deltar);
            pq->add_request(Request{}, client3, req_params);
            EXPECT_EQ(0, pq->client_map[client3]->deltar);
        } // TEST

        TEST(dmclock_server, queue_empty) {
            using ClientId = int;
            using Queue = dmc::PullPriorityQueue<ClientId, Request, false>;
            using QueueRef = std::unique_ptr<Queue>;

            ClientId client1 = 17;
            ClientId client2 = 98;
            ClientId client3 = 32;

            dmc::ClientInfo info1(100, 1.0, 0.0, dmc::ClientType::R);
            dmc::ClientInfo info2(0, 1.0, 0.0, dmc::ClientType::B);
            dmc::ClientInfo info3(0, 1.0, 0.0, dmc::ClientType::A);

            QueueRef pq;

            auto client_info_f = [&](ClientId c) -> const dmc::ClientInfo * {
                if (client1 == c) return &info1;//return &info1;
                else if (client2 == c) return &info2;
                else if (client3 == c) return &info3;
                else {
                    ADD_FAILURE() << "client info looked up for non-existant client";
                    return nullptr;
                }
            };

            pq = QueueRef(new Queue(client_info_f, 900, 30, false));
            ReqParams req_params(1, 1);

            EXPECT_TRUE(pq->empty());
            pq->add_request(Request{}, client1, req_params);
            EXPECT_FALSE(pq->empty());
            pq->pull_request();
            EXPECT_TRUE(pq->empty());

            pq->add_request(Request{}, client2, req_params);
            EXPECT_FALSE(pq->empty());
            pq->pull_request();
            EXPECT_TRUE(pq->empty());

            pq->add_request(Request{}, client3, req_params);
            EXPECT_FALSE(pq->empty());
            pq->pull_request();
            EXPECT_TRUE(pq->empty());
        } // TEST
    } // namespace dmclock
} // namespace crimson
