/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef UNIT_TEST_HPP
#define UNIT_TEST_HPP

#include "cassandra.h"
#include "connector.hpp"
#include "future.hpp"
#include "mockssandra.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>
#include <uv.h>

#define PROTOCOL_VERSION CASS_PROTOCOL_VERSION_V5
#define PORT 9042
#define WAIT_FOR_TIME 5 * 1000 * 1000 // 5 seconds
#define DEFAULT_NUM_NODES 1
#define DEFAULT_OUTAGE_PLAN_DELAY 500

class Unit : public testing::Test {
public:
  /**
   * Outage plan for simulating server faults.
   */
  class OutagePlan {
  public:
    /**
     * Type of action to occur during loop execution.
     */
    enum Type { START_NODE, STOP_NODE, ADD_NODE, REMOVE_NODE };

    /**
     * Action to take place during loop execution.
     */
    struct Action {
      /**
       * Constructor.
       *
       * @param type Action type.
       * @param node Node to take action on.
       * @param delay_ms Delay (in milliseconds) before action takes place.
       */
      Action(Type type, size_t node, uint64_t delay_ms);

      Type type;
      size_t node;
      uint64_t delay_ms;
    };
    typedef datastax::internal::Vector<Action> Actions;

    /**
     * Constructor.
     *
     * @param loop Loop to run the outage plan on.
     * @param cluster Mock cluster instance.
     */
    OutagePlan(uv_loop_t* loop, mockssandra::SimpleCluster* cluster);

    /**
     * Create a start node action.
     *
     * @param node Node to start.
     * @param delay_ms Delay in milliseconds to wait before starting node.
     */
    void start_node(size_t node, uint64_t delay_ms = DEFAULT_OUTAGE_PLAN_DELAY);
    /**
     * Create a stop node action.
     *
     * @param node Node to stop.
     * @param delay_ms Delay in milliseconds to wait before stopping node.
     */
    void stop_node(size_t node, uint64_t delay_ms = DEFAULT_OUTAGE_PLAN_DELAY);
    /**
     * Create a add node action.
     *
     * @param node Node to add.
     * @param delay_ms Delay in milliseconds to wait before adding node.
     */
    void add_node(size_t node, uint64_t delay_ms = DEFAULT_OUTAGE_PLAN_DELAY);
    /**
     * Create a remove node action.
     *
     * @param node Node to remove.
     * @param delay_ms Delay in milliseconds to wait before removing node.
     */
    void remove_node(size_t node, uint64_t delay_ms = DEFAULT_OUTAGE_PLAN_DELAY);

    /**
     * Start the actions.
     *
     * @param future Future to set when outage plan is running.
     */
    void
    run(datastax::internal::core::Future::Ptr future = datastax::internal::core::Future::Ptr());

    /**
     * Stop the outage plan; must be executed on the same thread that started
     * the actions.
     */
    void stop();

    /**
     * Check to see if all the actions are complete.
     *
     * @return True if outage plan is complete; false otherwise.
     */
    bool is_done();

  private:
    void next();

  private:
    void on_timeout(Timer* timer);
    void handle_timeout();

  private:
    Timer timer_;
    Actions::const_iterator action_it_;
    Actions actions_;
    uv_loop_t* loop_;
    mockssandra::SimpleCluster* cluster_;
    datastax::internal::core::Future::Ptr future_;
  };

public:
  class ExecuteOutagePlan : public Task {
  public:
    ExecuteOutagePlan(OutagePlan* outage_plan, datastax::internal::core::Future::Ptr future)
        : outage_plan_(outage_plan)
        , future_(future) {}
    virtual void run(EventLoop* event_loop) { outage_plan_->run(future_); }

  private:
    OutagePlan* outage_plan_;
    datastax::internal::core::Future::Ptr future_;
  };

  class StopOutagePlan : public Task {
  public:
    StopOutagePlan(OutagePlan* outage_plan, datastax::internal::core::Future::Ptr future)
        : outage_plan_(outage_plan)
        , future_(future) {}
    virtual void run(EventLoop* event_loop) {
      outage_plan_->stop();
      future_->set();
    }

  private:
    OutagePlan* outage_plan_;
    datastax::internal::core::Future::Ptr future_;
  };

public:
  /**
   * Constructor.
   */
  Unit();
  /**
   * Destructor.
   */
  virtual ~Unit();

  /**
   * Set the output log level for the test. This will output log messages to
   * stderr that meet the log level provided.
   *
   * @param output_log_level The log level to output to stderr.
   */
  void set_output_log_level(CassLogLevel output_log_level);

  /**
   * Create the default simple request handler for use with mockssandra.
   *
   * @return Simple request handler instance.
   */
  static const mockssandra::RequestHandler* simple();
  /**
   * Create the default authentication request handler for use with mockssandra.
   *
   * @return Simple request handler instance.
   */
  static const mockssandra::RequestHandler* auth();

  /**
   * Setup the cluster to use SSL and return a connection settings object with
   * a SSL context, a SSL certificate, and hostname resolution enabled.
   *
   * @param cluster Mockssandra cluster to apply SSL settings to
   * @param cn SSL common name (e.g. hostname or IP address depending on SSL
   *           peer verification)
   * @return A connection settings object setup to use SSL.
   */
  datastax::internal::core::ConnectionSettings use_ssl(mockssandra::Cluster* cluster,
                                                       const datastax::String& cn = "");

  /**
   * Add criteria to the search criteria for incoming log messages.
   *
   * @param criteria Criteria to add.
   * @param severity Severity level to match (Optional: defaults to any severity).
   */
  void add_logging_critera(const String& criteria, CassLogLevel severity = CASS_LOG_LAST_ENTRY);
  /**
   * Get the number of log messages that matched the search criteria.
   *
   * @return Number of matched log messages.
   */
  int logging_criteria_count();

  /**
   * Reset the logging criteria; clears all criteria and resets count.
   */
  void reset_logging_criteria();

private:
  /**
   * Log the message from the driver (callback).
   *
   * @param message Log message structure from the driver.
   * @param data Object passed from the driver (Unit class instance).
   */
  static void on_log(const CassLogMessage* message, void* data);

private:
  CassLogLevel output_log_level_;
  datastax::internal::Map<CassLogLevel, datastax::internal::Vector<datastax::String> >
      logging_criteria_;
  int logging_criteria_count_;
  uv_mutex_t mutex_;
};

#endif // UNIT_TEST_HPP
