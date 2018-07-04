cmake_minimum_required(VERSION 3.8)
project(CRF___0_58)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        java/CRFPP_wrap.cxx
        perl/CRFPP_wrap.cxx
        python/CRFPP_wrap.cxx
        ruby/CRFPP_wrap.cpp
        sdk/example.cpp
        swig/CRFPP.i
        swig/CRFPP_wrap.c
        swig/version.h
        common.h
        crf_learn.cpp
        crf_test.cpp
        crfpp.h
        darts.h
        encoder.cpp
        encoder.h
        feature.cpp
        feature_cache.cpp
        feature_cache.h
        feature_index.cpp
        feature_index.h
        freelist.h
        lbfgs.cpp
        lbfgs.h
        libcrfpp.cpp
        mmap.h
        node.cpp
        node.h
        param.cpp
        param.h
        path.cpp
        path.h
        scoped_ptr.h
        stream_wrapper.h
        tagger.cpp
        tagger.h
        thread.h
        timer.h
        winmain.h)

add_executable(CRF___0_58 ${SOURCE_FILES})