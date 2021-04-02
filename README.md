# Task-Scheduler
This C++ library provides code for scheduling and parallel execution of tasks.
This is an improved version of my older [multithread-task](https://github.com/asmith-git/multithread-task) project, which itself is a port of the task management system in a game engine I wrote a number of years ago.

## Extensions
This library has some optional features that can be enabled with compiler constants:
#### ANVIL_TASK_DELAY_SCHEDULING
This adds the virtual IsReadyToExecute() function to Task, which will prevent a Task from being executed until this returns true. This can be useful for holding tasks in the scheduler until some resource becomes available.
#### ANVIL_TASK_CALLBACKS
Adds the virtual functions OnScheduled(), OnBlock(), OnResume() and OnCancel() functions to task. These are called when the state of the task is about to be changed, which is usefull for debugging or monitoring the status of the scheduler.
#### ANVIL_TASK_HAS_EXCEPTIONS
Allows tasks to throw exceptions when executing, which will be caught and rethrown by Wait()
#### ANVIL_TASK_MEMORY_OPTIMISED
Reduces the memory footprint of Task objects to around half the normal size.
Incompatible with ANVIL_TASK_HAS_EXCEPTIONS, ANVIL_DEBUG_TASKS and ANVIL_TASK_EXTENDED_PRIORITY
#### ANVIL_TASK_EXTENDED_PRIORITY
This allows programming of how the scheduler orders tasks that have equal priority using the GetExtendedPriority() function.
#### ANVIL_NO_EXECUTE_ON_WAIT
Wait() will put the thread to sleep instead of calling Yield(). This should be used when you don't want tasks executing on a particular thread.
#### ANVIL_DEBUG_TASKS
Log information about task scheduling and execution for debugging purposes.


## Usage Examples
### Creating a Task
If none of the optional extensions are enabled, then creating a custom Task is easy. All we need to do is override the OnExecution() function with our custom behaviour.
```cpp
// This task prints a message to std::cout, fairly simple
class MyTask final : public anvil::Task {
private:
	std::string _message;
protected:
	// This is the function that gets called when the scheduler has decided to run the task
	void OnExecution() final {
		std::cout << _message << std::endl;
	}
public:
	MyTask(std::string message) :
		Task(),
		_message(std::move(message))
	{}

	virtual ~MyTask() {

	}

};
```
### Scheduling Tasks
Now we have a task to run, we can look at how scheduling works.
```cpp
int main{

	// First we need to create the task scheduler that will run tasks
	anvil::ExampleSchedulerSingleThreaded scheduler;

	{
		// Now let's make some tasks
		MyTask task1("Hello World!");
		MyTask task2("This is another task");

		// We can send the tasks to the scheduler and they will run in parallel
		scheduler.Schedule(task1);
		scheduler.Schedule(task2);

		// We can do something else on the thread now
    		std::cout << "This is parallel with tasks" << std::endl;

		// Now let's wait for the tasks to finish executing
		task1.Wait();
		task2.Wait();
    
    		std::cout << "All tasks complete" << std::endl;
	}
}
```
### Simplifying The Code
If we don't mind adding a small overhead to task execution then we can simplify it so that we don't need to write a whole class. We can instead use TaskFunctional to pass a std::function, lambda or a C function pointer into the scheduler.
```cpp
int main{

	// First we need to create the task scheduler that will run tasks
	anvil::ExampleSchedulerSingleThreaded scheduler;

	{
		// Use a more friendly name
		typedef anvil::TaskFunctional<std::function<void()>> EasyTask;

		// We can program the tasks to do anything we want now, but these ones will
		// do the same thing as MyTask
		EasyTask task1([]()->void { std::cout << "Hello World!" << std::endl; });
		EasyTask task2([]()->void { std::cout << "This is another task" << std::endl; });

		// Sending task to the scheduler works the same for all tasks
		scheduler.Schedule(task1);
		scheduler.Schedule(task2);
    
    		std::cout << "This is parallel with tasks" << std::endl;
    
		task1.Wait();
		task2.Wait();
    
    		std::cout << "All tasks complete" << std::endl;
	}
}
```
