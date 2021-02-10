cmake_minimum_required(VERSION 3.17)
project(extensions)

set(CMAKE_CXX_STANDARD 14)

include_directories(extensions)
include_directories(extensions/cat)
include_directories(extensions/put)
include_directories(/usr/local/include/ns3-dev/)
include_directories(/usr/local/include/ns3-dev/ns3/ndnSIM/)

add_executable(extensions
        extensions/cat/catChunks.cpp
        extensions/cat/catChunks.hpp
        extensions/cat/common.hpp
        extensions/cat/consumerApp.hpp
        extensions/cat/consumer.cpp
        extensions/cat/consumer.hpp
        extensions/cat/data-fetcher.cpp
        extensions/cat/data-fetcher.hpp
        extensions/cat/discover-version.cpp
        extensions/cat/discover-version.hpp
        extensions/cat/options.hpp
        extensions/cat/pipeline-interests-adaptive.cpp
        extensions/cat/pipeline-interests-adaptive.hpp
        extensions/cat/pipeline-interests-aimd.cpp
        extensions/cat/pipeline-interests-aimd.hpp
        extensions/cat/pipeline-interests-cubic.cpp
        extensions/cat/pipeline-interests-cubic.hpp
        extensions/cat/pipeline-interests-fixed.cpp
        extensions/cat/pipeline-interests-fixed.hpp
        extensions/cat/pipeline-interests.cpp
        extensions/cat/pipeline-interests.hpp
        extensions/cat/statistics-collector.cpp
        extensions/cat/statistics-collector.hpp
        extensions/put/common.hpp
        extensions/put/producer.cpp
        extensions/put/producer.hpp
        extensions/put/putChunks.cpp
        extensions/put/putChunks.hpp
        scenarios/ndn-simple.cpp
        extensions/netblt/NetBLT.cpp extensions/netblt/NetBLT.h extensions/netblt/NetBLTApp.cpp extensions/netblt/NetBLTApp.h extensions/netblt/options.h scenarios/multiple.cpp scenarios/typologyReader.cpp scenarios/ndn-simple-mpi.cpp)
