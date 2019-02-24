#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <memory>
#include <functional>

namespace VWNet
{
  class ThreadPool
  {
  public:
    using Task = std::function<void(void)>;
    ThreadPool(uint16_t thread_count=std::thread::hardware_concurrency()) noexcept
      :Running_(true),
       ThreadCount_(thread_count)
    {}
    ~ThreadPool()
    {
      Stop();
    }

    template<typename T>
    void Commit(T&& task)
    {
      {
        std::lock_guard<std::mutex> guard(Mutex_);
        Tasks_.emplace(std::forward<T>(task));
      }
      TaskReady_.notify_one();
    }

    template<typename T, typename... Args>
    void Commit(T&& task, Args&&... args)
    {
      Commit(std::move(std::bind(std::forward<T>(task),std::forward<Args>(args)...)));
    }
    
    void Start()
    {
      for(uint16_t i = 0; i < ThreadCount_; i++)
      {
        Threads_.emplace_back(std::make_shared<std::thread>(std::bind(&ThreadPool::Work,this)));
      }
    }
    
    void Stop()
    {
      if(Running_)
      {
        Running_= false;
        TaskReady_.notify_all();
        for(auto t : Threads_)
        {
          if(t->joinable())
            t->join();  
        }
      }
      Threads_.clear();
    }

    void SetThreadNum(uint16_t thread_count) { ThreadCount_ = thread_count;}
    uint16_t ThreadNum() const { return ThreadCount_; }

  private:
    void Work()
    {
      //std::unique_lock<std::mutex> guard(Mutex_,std::defer_lock);
      //std::unique_lock<std::mutex> locker(Mutex_);
      //std::condition_variable TaskReady_;              
      //constexpr std::chrono::milliseconds wait_timeout = 1
      while(Running_)
      {
        Task task;
        {
          std::lock_guard<std::mutex> guard(Mutex_);
          if(Tasks_.empty())
          {
            // wait() will automatically unlock mutex to allow other blocked threads to continue
            // and it will lock mutex agian once wait() returns.
            TaskReady_.wait(Mutex_,[this](){return !Tasks_.empty() || !Running_;}); 
          }
          if(!Tasks_.empty())
          {
            task = std::move(Tasks_.front());  
            Tasks_.pop();
          }
          else continue;
        }
        task();
      }
    }

  private:
    std::mutex Mutex_;                                         
    std::condition_variable_any TaskReady_;
    std::queue<Task> Tasks_;                                    
    bool Running_;                                             
    uint16_t ThreadCount_;                                         
    std::vector<std::shared_ptr<std::thread>> Threads_;        
  };  
}



