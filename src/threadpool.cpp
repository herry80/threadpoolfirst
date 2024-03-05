#include"threadpool.hpp"
#include<functional>
#include<chrono>
#include<mutex>
const int TASK_MAX_THRESHHOLD=1024;
const int THREAD_MAX_THRESHHOLD=100;
const int THREAD_MAX_IDLE_TIME=60;//秒

ThreadPool::ThreadPool():initThreadSize_(0),taskSize_(0),taskQueMaxThreshHold(TASK_MAX_THRESHHOLD),PoolMode_(PoolMode::MODE_FIXED)
,isPoolRuning_(false),idleThreadSize_(0),threadSizeTreshHold_(THREAD_MAX_THRESHHOLD),curThreadSize_(0)
{}
ThreadPool::~ThreadPool()
{
    isPoolRuning_=false;
    //notEmpty_.notify_all();
    //等待线程全部执行完
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();
    exitCond_.wait(lock,[&]()->bool{return threads_.size()==0;});
}
//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if(checkRuningState())return;
    PoolMode_=mode;
}
//设置tasK任务队列上限的阈值
void ThreadPool::setTaskQueMaxThreshHold(size_t threshhold)
{
    if(checkRuningState())return;
    taskQueMaxThreshHold=threshhold;
}
//设置线程上限阈值(cache模式下)
void ThreadPool::setThreadMaxThreshHold(size_t threadhold)
{
    if(checkRuningState())return;
    if(PoolMode_==PoolMode::MODE_CACHED){
        threadSizeTreshHold_=threadhold;
    }
}
//给线程池提交任务 用户提交任务（生产者）
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    //获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    //线程通信： 等待任务队列有空余
   // while(taskQue_.size()==taskQueMaxThreshHold){
        //notFull_.wait(lock);//等待，并且释放锁
    //}
    //更简便的写法
    //notFull_.wait(lock,[&]()->bool{return taskQue_.size()<taskQueMaxThreshHold;});
    //我们想用户提交任务，最长不能阻塞超过1s，否则则提交任务失败，可以用c++11的wait_for
    if(!notFull_.wait_for(lock,std::chrono::seconds(1),[&]()->bool{return taskQue_.size()<(size_t)taskQueMaxThreshHold;}))
    {
        //说明notFull等待1s后，条件依然没有满足
        std::cerr<<"task queue is full,submit fail!"<<std::endl;
        return Result(sp,false);
    }
    //如果有空余，把任务放到任务队列中
    taskQue_.emplace(sp);
    taskSize_++;
    //因为新放了任务，任务队列肯定不空，通知
    notEmpty_.notify_all();
    //cache模式                                任务数量大于空闲数量               总线程量小于线程上限阈值
    if (PoolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeTreshHold_)
    {
        std::cout << ">>> create new thread..." << std::endl;
        //创建新线程
        auto ptr=std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
        int ThreadId=ptr->getId();
        threads_.emplace(ThreadId,std::move(ptr));
        //threads_.emplace_back(std::move(ptr));
        threads_[ThreadId]->start();
        idleThreadSize_++;
        curThreadSize_++;
    }
    //返回结果
    return Result(sp);
}
//开启线程池
void ThreadPool::start(size_t initThreadSize)
{
    //设置线程池的状态
    isPoolRuning_=true;
    //记录线程数量
    initThreadSize_=initThreadSize;
    curThreadSize_=initThreadSize;
    //创建线程对象
    for(size_t i=0;i<initThreadSize_;++i){
        auto ptr=std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
        int ThreadId=ptr->getId();
        threads_.emplace(ThreadId,std::move(ptr));
        //threads_.emplace_back(std::move(ptr));
    }
    //启动所有线程
    for(size_t i=0;i<initThreadSize_;++i){
        threads_[i]->start();//线程类型里的方法
        //注意：启动一个线程，我们得有一个线程函数！
        idleThreadSize_++;//空闲线程增加
    }
}
//定义线程函数   从任务队列里消费任务(消费者)
void ThreadPool::threadFunc(int threadid)
{
    auto lastTime=std::chrono::high_resolution_clock().now();
    for(;;)//不断从任务队列中取任务
    {
        std::shared_ptr<Task> task;
        {
            // 获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);
            std::cout << "tid:" << std::this_thread::get_id()
				<< "尝试获取任务..." << std::endl;
            //cache模式下，有可能创建了很多线程，规定空闲时间超过60s,就把多余的线程回收
            //超过initThreadSize_数量的线程进行回收
            while(taskQue_.size()==0){
                // 线程池要结束
                if (!isPoolRuning_)
                {
                    threads_.erase(threadid);
                    std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
                              << std::endl;
                    exitCond_.notify_all();
                    return; // 线程函数结束，线程结束
                }
                if (PoolMode_ == PoolMode::MODE_CACHED)
                {
                    // 条件变量超时返回
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now(); // 当前时间
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
                        {
                            // 开始回收当前线程
                            /*
                            std::atomic_int32_t idleThreadSize_;//空闲线程的数量     减一
                            std::atomic_int32_t curThreadSize_;//当前线程的总数量    减一
                            */
                            // 通过threadid=>当前线程//从map中删除
                            threads_.erase(threadid);
                            idleThreadSize_--;
                            curThreadSize_--;

                            std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
                                      << std::endl;
                            return;
                        }
                    }
                }
                else
                {
                    // 等待notEmpty_条件
                    notEmpty_.wait(lock);
                }
            }
            
            idleThreadSize_--;//空闲线程减减
            std::cout << "tid:" << std::this_thread::get_id()
				<< "获取任务成功..." << std::endl;
            // 从任务队列中取一个任务出来
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

             //如果依然有剩余任务，继续通知其他线程执行任务
             if(taskQue_.size()>0){
                notEmpty_.notify_all();
             }
             //取出一个任务，进行通知，可以继续生产
             notFull_.notify_all();

        }//释放锁,任务执行之前释放锁
        //当前线程负责执行这个任务
        if(task)
        {
            //task->run();
            task->exec();
        }
        idleThreadSize_++;
        lastTime=std::chrono::high_resolution_clock().now();//更新最后一次运行的时间
    }
}
//检查pool的运行状态
bool ThreadPool::checkRuningState()const
{
    return isPoolRuning_;
}
//*******************************************************************************线程类型方法实现
//线程类构造函数
Thread::Thread(ThreadFunc func):func_(func),threadId_(generateId++)
{}
//线程类析构函数
Thread::~Thread(){}
//启动线程
void Thread::start()
{
    //创建一个线程执行一个线程函数
    std::thread t(func_,threadId_);
    t.detach();//分离
}
 //获取线程id
int Thread::getId()const
{
    return threadId_;
}
int Thread::generateId=0;
//**********************************************************************************Result实现
Result::Result(std::shared_ptr<Task> sp,bool tag):task_(sp),isValid_(tag){
    task_->setResult(this);
}
//setVal方法，获取任务执行完的返回值，就是把线程函数里的返回值给到any_；
void Result::setVal(Any any){
    this->any_=std::move(any);
    sem_.post();
}
//get()方法，用户调用这个方法获取task的返回值
Any Result::get(){
    if(!isValid_){
        return "";
    }
    sem_.wait();
    return std::move(any_);
}
//*************************Task类实现
Task::Task():result_(nullptr){}

void Task::exec()
{
    if(result_!=nullptr){
        result_->setVal(run());
    }
}
void Task::setResult(Result* res)
{
    result_=res;
}