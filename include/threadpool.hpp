#pragma once
#include<iostream>
#include<vector>
#include<memory>
#include<queue>
#include<atomic>
#include<mutex>
#include<thread>
#include<condition_variable>
#include<functional>
#include<future>
#include<unordered_map>
//Any类实现，可以接收任意一个类型
class Any
{
public:
    Any(){}
    ~Any(){}
    Any(const Any&)=delete;
    Any& operator=(const Any&)=delete;
    Any(Any&&)=default;
    Any& operator=(Any&&)=default;
    //主要的构造函数如下
    template<class T>
    Any(T data){
        base_=std::make_unique<Derive<T>>(data);
    }

    //把Any存储的data提取出来
    template<class T>
    T cast_()
    {
        //将基类指针转成派生类指针
        Derive<T> *pd=dynamic_cast<Derive<T>*>(base_.get());
        if(pd==nullptr){
            throw "type is unmatch!";
        }
        return pd->data_;
    }
private:
//基类类型
    class Base
    {
    public:
        virtual ~Base()=default;
    };
    //派生类类型
    template<class T>
    class Derive:public Base{
    public:
        Derive(T data):data_(data){}
        T data_;
    };
private:
//定义一个基类指针
    std::unique_ptr<Base> base_;
};

//实现一个信号量，通过互斥锁和条件变量
class Semaphore
{
public:
    Semaphore(int limit=0):resLimit_(limit),isExit_(false){}
    ~Semaphore(){isExit_=true;}

    //获取一个信号量资源
    void wait(){
        if(isExit_)return;
        std::unique_lock<std::mutex> lock(mtx_);
        //等待信号量有资源
        cond_.wait(lock,[&]()->bool{return resLimit_>0;});
        resLimit_--;
    }
    //增加一个信号量资源
    void post(){
        if(isExit_)return;
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    std::atomic_bool isExit_;
    int resLimit_;//资源计数
    std::mutex mtx_;
    std::condition_variable cond_;
};

class Task;
//实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
    Result(std::shared_ptr<Task> sp,bool tag=true);
    ~Result()=default;

    //setVal方法，获取任务执行完的返回值，就是把线程函数里的返回值给到any_；
    void setVal(Any any);
    //get()方法，用户调用这个方法获取task的返回值
    Any get();
private:
    Any any_;//存储返回值类型
    Semaphore sem_;//线程通信的信号量
    std::shared_ptr<Task> task_;//指向对应获取返回值的返回对象
    std::atomic_bool isValid_;//返回值是否有效
};
//任务抽象类
class Task
{
public:
    Task();
    ~Task()=default;
    virtual Any run()=0;
    
    void exec();
    void setResult(Result*);
private:
    Result* result_;//裸指针就行，不要智能指针，循环应用
};

//线程池模式
enum PoolMode
{
    MODE_FIXED,//固定数量
    MODE_CACHED//线程数量可动态增长
};
//线程类型
class Thread
{
public:
    //线程函数类型
    using ThreadFunc=std::function<void(int)>;
    Thread(ThreadFunc func);
    ~Thread();
    //启动线程
    void start();
    //获取线程id
    int getId()const;
private:
    ThreadFunc func_;//函数对象类型,保存线程函数
    static int generateId;
    int threadId_;//保存线程id；
};
//线程池类型
class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();
    //设置线程池的工作模式
    void setMode(PoolMode mode);
    //设置tasK任务队列上限的阈值
    void setTaskQueMaxThreshHold(size_t threshhold);
    //设置线程上限阈值(cache模式下)
    void setThreadMaxThreshHold(size_t threadhold);
    //给线程池提交任务
    Result submitTask(std::shared_ptr<Task> sp);
    //开启线程池
    void start(size_t initThreadSize=std::thread::hardware_concurrency());

    ThreadPool(const ThreadPool&)=delete;
    ThreadPool& operator=(const ThreadPool&)=delete;
private:
//定义线程函数
void threadFunc(int threadid);
//检查pool的运行状态
bool checkRuningState()const;
private:
    //std::vector<std::unique_ptr<Thread>> threads_;//线程列表
    std::unordered_map<int,std::unique_ptr<Thread>> threads_;//线程列表
    size_t initThreadSize_;//初始的线程数量
    size_t threadSizeTreshHold_;//线程数量上限阈值
    std::atomic_uint32_t idleThreadSize_;//空闲线程的数量
    std::atomic_uint32_t curThreadSize_;//当前线程的总数量

    std::queue<std::shared_ptr<Task>> taskQue_;//任务队列   最好用智能指针
    std::atomic_uint32_t taskSize_;//任务的数量
    size_t taskQueMaxThreshHold;//任务队列数量的上限的阈值

    std::mutex taskQueMtx_;//任务队列的线程安全
    std::condition_variable notFull_;//任务队列不满
    std::condition_variable notEmpty_;//任务队列不空
    std::condition_variable exitCond_;//等待线程数量全部回收

    PoolMode PoolMode_;//当前工作模式

    std::atomic_bool isPoolRuning_;//当前线程池的状态

};