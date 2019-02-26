## It's a common network library based on epoll and thread pool in C++11.
*It faciliates fast building server/client application with business logic.*
-----------------------------
**Setup & build**
1. **create bulid folders**   
$ `mkdir debug & make release`
2. **enter folder debug/release and run below command to generate makefiles**    
$ `cmake -DCMAKE_BUILD_TYPE=Debug ../`    
$ `cmake -DCMAKE_BUILD_TYPE=Release ../`   
3. **build the project**    
$ `make`
--------------------------------------------------
**simply buid the test app**  
`g++ -std=c++11 -o test.out thread_pool.cpp -lpthread`      
**create a dummy file for testing**   
dd if=/dev/zero of=50M.file bs=1M count=50
