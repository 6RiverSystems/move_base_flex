#ifndef MBF_ABSTRACT_NAV__ABSTRACT_ACTION_H_
#define MBF_ABSTRACT_NAV__ABSTRACT_ACTION_H_

#include <actionlib/server/action_server.h>
#include <boost/bimap/bimap.hpp>
#include <mbf_abstract_nav/MoveBaseFlexConfig.h>
#include "mbf_abstract_nav/robot_information.h"

namespace mbf_abstract_nav{


template <typename Action, typename Execution>
class AbstractAction
{
 public:
  typedef boost::bimaps::bimap<uint8_t, std::string> SlotGoalIdMap;

  typedef boost::shared_ptr<AbstractAction> Ptr;

  typedef typename actionlib::ActionServer<Action>::GoalHandle GoalHandle;
  typedef boost::function<void (GoalHandle &goal_handle, Execution &execution)> RunMethod;

  AbstractAction(
      const std::string& name,
      const RobotInformation &robot_info,
      const RunMethod run_method
  ) : name_(name), robot_info_(robot_info), run_(run_method){}

  void start(
      GoalHandle goal_handle,
      typename Execution::Ptr execution_ptr
  )
  {
    boost::lock_guard<boost::mutex> lock_guard(map_mtx_);
    if(concurrency_slots_.empty()){

    }
    typename SlotGoalIdMap::left_const_iterator slot
        = concurrency_slots_.left.find(goal_handle.getGoal()->concurrency_slot);
    if(slot != concurrency_slots_.left.end()) // if there is a plugin running on the same slot, cancel it // TODO make thread safe
    {
      typename std::map<const std::string, const typename Execution::Ptr>::const_iterator elem
          = executions_.find(slot->second);
      if(elem != executions_.end())
      {
        elem->second->cancel();
      }
      concurrency_slots_.left.erase(slot->first);
    }
    goal_ids_.push_back(goal_handle.getGoalID().id);
    concurrency_slots_.insert(SlotGoalIdMap::value_type(goal_handle.getGoal()->concurrency_slot, goal_handle.getGoalID().id));
    executions_.insert(std::pair<const std::string, const typename Execution::Ptr>(goal_handle.getGoalID().id, execution_ptr));
    threads_.create_thread(boost::bind(&AbstractAction::runAndCleanUp, this, goal_handle, execution_ptr));
  }

  void cancel(GoalHandle &goal_handle){
    boost::lock_guard<boost::mutex> lock_guard(map_mtx_);
    typename std::map<const std::string, const typename Execution::Ptr>::const_iterator
        elem = executions_.find(goal_handle.getGoalID().id);
    if(elem != executions_.end())
    {
      elem->second->cancel();
    }
  }

  virtual void before(GoalHandle& goal_handle, Execution& execution){};

  virtual void after(GoalHandle& goal_handle, Execution& execution){};

  void runAndCleanUp(GoalHandle goal_handle, typename Execution::Ptr execution_ptr){
    map_mtx_.lock();
    before(goal_handle, *execution_ptr);
    map_mtx_.unlock();

    run_(goal_handle, *execution_ptr);

    ROS_DEBUG_STREAM("Finished action run method, waiting for execution thread to finish.");
    execution_ptr->join();
    ROS_DEBUG_STREAM("Execution thread stopped, cleaning up the execution object map and the slot map");
    boost::lock_guard<boost::mutex> lock_guard(map_mtx_);
    after(goal_handle, *execution_ptr);
    executions_.erase(goal_handle.getGoalID().id);
    concurrency_slots_.right.erase(goal_handle.getGoalID().id);
    goal_ids_.erase(std::remove(goal_ids_.begin(), goal_ids_.end(), goal_handle.getGoalID().id), goal_ids_.end());
    ROS_DEBUG_STREAM("Exiting run method with goal status: " << goal_handle.getGoalStatus().text << " and code: "
        << static_cast<int>(goal_handle.getGoalStatus().status));
  }

  void reconfigureAll(
      mbf_abstract_nav::MoveBaseFlexConfig &config, uint32_t level)
  {
    boost::lock_guard<boost::mutex> lock_guard(map_mtx_);
    typename std::map<const std::string, const typename Execution::Ptr>::iterator iter;
    for(iter = executions_.begin(); iter != executions_.end(); ++iter)
    {
      iter->second->reconfigure(config);
    }
  }

  void cancelAll()
  {
    ROS_INFO_STREAM_NAMED(name_, "Cancel all goals for \"" << name_ << "\".");
    boost::lock_guard<boost::mutex> lock_guard(map_mtx_);
    typename std::map<const std::string, const typename Execution::Ptr>::iterator iter;
    for(iter = executions_.begin(); iter != executions_.end(); ++iter)
    {
      iter->second->cancel();
    }
    threads_.join_all();
  }

  const std::string &name_;
  const RobotInformation &robot_info_;

  RunMethod run_;
  boost::thread_group threads_;
  std::map<const std::string, const typename Execution::Ptr> executions_;
  SlotGoalIdMap concurrency_slots_;
  std::vector<std::string> goal_ids_;
  boost::mutex map_mtx_;

};


}


#endif //MBF_ABSTRACT_NAV__ABSTRACT_ACTION_H_
