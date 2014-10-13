#include "ThreadPool.h"
#include "Common/CPUDetect.h"
#ifdef _WIN32
#include <windows.h>
#endif
using namespace Common;

ThreadPool::ThreadPool() : m_workflag(0), m_workercount(0), m_workers(16)
{
	m_working = true;
	for (size_t i = 0; i < cpu_info.logical_cpu_count; i++)
	{
		std::thread* current = new std::thread(&ThreadPool::Workloop, std::ref(*this), i);
		m_workerThreads.push_back(std::unique_ptr<std::thread>(current));
#ifdef _WIN32
		SetThreadPriority(current->native_handle(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
	}
}

ThreadPool::~ThreadPool()
{
	m_working = false;
	for (u32 i = 0; i < m_workerThreads.size(); i++)
	{
		std::thread* current = m_workerThreads[i].get();
		if (current->joinable())
		{
			current->join();
		}
	};
}

ThreadPool& ThreadPool::Getinstance()
{
	static ThreadPool instance;
	return instance;
}

void ThreadPool::NotifyWorkPending()
{
	ThreadPool::Getinstance().m_workflag++;
}

static SpinLock<true> workerLock;
void ThreadPool::RegisterWorker(IWorker* worker)
{
	workerLock.lock();
	ThreadPool& instance = ThreadPool::Getinstance();
	size_t count = instance.m_workercount.fetch_add(1);
	instance.m_workers[count] = worker;
	workerLock.unlock();
}
void ThreadPool::UnregisterWorker(IWorker* worker)
{
	workerLock.lock();
	ThreadPool& instance = ThreadPool::Getinstance();
	u32 count = instance.m_workercount.load();
	u32 index = 0;
	for (u32 i = 0; i < count; i++)
	{
		if (instance.m_workers[i] == worker)
		{
			index = i;
			break;
		}
	}
	instance.m_workers[index] = instance.m_workers[count - 1];
	instance.m_workercount--;
	workerLock.unlock();
}

void ThreadPool::Workloop(ThreadPool &state, size_t ID)
{
	while (state.m_working)
	{
		if (state.m_workflag.load() > ID)
		{
			bool worked = false;
			u32 count = state.m_workercount.load();
			for (u32 i = 0; i < count; i++)
			{
				IWorker* worker = state.m_workers[i];
				if (worker)
				{
					if (worker->NextTask())
					{
						worked = true;
						state.m_workflag--;
					}
				}
			}
			if (worked)
			{
				Common::YieldCPU();
				continue;
			}
		}
		SleepCurrentThread(1);
	}
}

AsyncWorker& AsyncWorker::Getinstance()
{
	static AsyncWorker intance;
	return intance;
}

AsyncWorker::AsyncWorker() : m_inputsize(0), m_TaskQueue()
{
	ThreadPool::RegisterWorker(this);
}

AsyncWorker::~AsyncWorker()
{
	ThreadPool::UnregisterWorker(this);
}

bool AsyncWorker::NextTask()
{
	if (m_inputsize.load() > 0)
	{
		std::function<void()> func;
		if (m_TaskQueue.try_pop(func))
		{
			func();
			m_inputsize--;
			return true;
		}
	}
	return false;
}

void AsyncWorker::ExecuteAsync(std::function<void()> &&func)
{
	AsyncWorker& instance = Getinstance();
	instance.m_inputsize++;
	instance.m_TaskQueue.push(std::move(func));
	ThreadPool::NotifyWorkPending();
}


