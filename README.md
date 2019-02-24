# This project is common network library based on epoll and thread pool in C++11.
# It faciliates fast building server/client application with business logic.

g++ -std=c++11 -o test.out thread_pool.cpp -lpthread
dd if=/dev/zero of=50M.file bs=1M count=50

# You can mkdir folder bulid and run below command to setup makefiles.
cmake -DCMAKE_BUILD_TYPE=Debug ../
cmake -DCMAKE_BUILD_TYPE=Release ../
