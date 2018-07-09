//
//  CRF++ -- Yet Another CRF toolkit
//
//  $Id: encoder.cpp 1601 2007-03-31 09:47:18Z taku $;
//
//  Copyright(C) 2005-2007 Taku Kudo <taku@chasen.org>
//
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <fstream>
#include "param.h"
#include "encoder.h"
#include "timer.h"
#include "tagger.h"
#include "lbfgs.h"
#include "common.h"
#include "feature_index.h"
#include "scoped_ptr.h"
#include "thread.h"

namespace CRFPP {
namespace {

inline size_t getCpuCount() {
  size_t result = 1;
#if defined(_WIN32) && !defined(__CYGWIN__)
  SYSTEM_INFO si;
  ::GetSystemInfo(&si);
  result = si.dwNumberOfProcessors;
#else
#ifdef HAVE_SYS_CONF_SC_NPROCESSORS_CONF
  const long n = sysconf(_SC_NPROCESSORS_CONF);
  if (n == -1) {
    return 1;
  }
  result = static_cast<size_t>(n);
#endif
#endif
  return result;
}

unsigned short getThreadSize(unsigned short size) {
    // 获取线程数
  if (size == 0) {  // 如果为0，就使用CPU和核数
    return static_cast<unsigned short>(getCpuCount());
  }
  return size;
}

bool toLower(std::string *s) {  // 大写转小写
  for (size_t i = 0; i < s->size(); ++i) {
    char c = (*s)[i];
    if ((c >= 'A') && (c <= 'Z')) {
      c += 'a' - 'A';
      (*s)[i] = c;
    }
  }
  return true;
}
}

class CRFEncoderThread: public thread { // 一个个的线程计算单元
 public:
  TaggerImpl **x; // 把线程的任务队列指向：  把tagger vector 的起点
		// 虽然，每个线程都指向的是总的tagger 列表，但是后面他们会抽样从里面执行
  unsigned short start_i;  // 本线程的线程号
  unsigned short thread_num;  // 所有的线程数
  int zeroone;  // 如果对本线程的某个句子预测错误，就+1
  int err;  // 本线程上，分配了一些句子，每个句子又是由一些行组成，这个err反应对这些行的预测错误个数
  size_t size;  // 总的tagger 数量
  double obj;  // 目标值  -log(y|x)
  std::vector<double> expected;  // 长度=len(特征字典)  据说是梯度？

  void run() {
    obj = 0.0;
    err = zeroone = 0;
    std::fill(expected.begin(), expected.end(), 0.0);  // 全部填充0
    for (size_t i = start_i; i < size; i += thread_num) {
	    // 作者很喜欢使用这种 base+offset的编程模式哈：就相当远一排子弹，然后左轮枪旋转着消费一样
	    // 这里的i 就是本线程拿到的tagger
      obj += x[i]->gradient(&expected[0]);  // 对该tagger计算梯度
      int error_num = x[i]->eval();
      err += error_num;
      if (error_num) {
        ++zeroone;
      }
    }
  }
};

bool runMIRA(const std::vector<TaggerImpl* > &x,
             EncoderFeatureIndex *feature_index,
             double *alpha,
             size_t maxitr,
             float C,
             double eta,
             unsigned short shrinking_size,
             unsigned short thread_num) {
  std::vector<unsigned char> shrink(x.size());
  std::vector<float> upper_bound(x.size());
  std::vector<double> expected(feature_index->size());

  std::fill(upper_bound.begin(), upper_bound.end(), 0.0);
  std::fill(shrink.begin(), shrink.end(), 0);

  int converge = 0;
  int all = 0;
  for (size_t i = 0; i < x.size(); ++i) {
    all += x[i]->size();
  }

  for (size_t itr = 0; itr < maxitr; ++itr) {
    int zeroone = 0;
    int err = 0;
    int active_set = 0;
    int upper_active_set = 0;
    double max_kkt_violation = 0.0;

    for (size_t i = 0; i < x.size(); ++i) {
      if (shrink[i] >= shrinking_size) {
        continue;
      }

      ++active_set;
      std::fill(expected.begin(), expected.end(), 0.0);
      double cost_diff = x[i]->collins(&expected[0]);
      int error_num = x[i]->eval();
      err += error_num;
      if (error_num) {
        ++zeroone;
      }

      if (error_num == 0) {
        ++shrink[i];
      } else {
        shrink[i] = 0;
        double s = 0.0;
        for (size_t k = 0; k < expected.size(); ++k) {
          s += expected[k] * expected[k];
        }

        double mu = std::max(0.0, (error_num - cost_diff) / s);

        if (upper_bound[i] + mu > C) {
          mu = C - upper_bound[i];
          ++upper_active_set;
        } else {
          max_kkt_violation = std::max(error_num - cost_diff,
                                   max_kkt_violation);
        }

        if (mu > 1e-10) {
          upper_bound[i] += mu;
          upper_bound[i] = std::min(C, upper_bound[i]);
          for (size_t k = 0; k < expected.size(); ++k) {
            alpha[k] += mu * expected[k];
          }
        }
      }
    }

    double obj = 0.0;
    for (size_t i = 0; i < feature_index->size(); ++i) {
      obj += alpha[i] * alpha[i];
    }

    std::cout << "iter="  << itr
              << " terr=" << 1.0 * err / all
              << " serr=" << 1.0 * zeroone / x.size()
              << " act=" <<  active_set
              << " uact=" << upper_active_set
              << " obj=" << obj
              << " kkt=" << max_kkt_violation << std::endl;

    if (max_kkt_violation <= 0.0) {
      std::fill(shrink.begin(), shrink.end(), 0);
      converge++;
    } else {
      converge = 0;
    }

    if (itr > maxitr || converge == 2) {
      break;  // 2 is ad-hoc
    }
  }

  return true;
}

bool runCRF(const std::vector<TaggerImpl* > &x,
            EncoderFeatureIndex *feature_index,
            double *alpha, // 特征函数的权重参数列表
            size_t maxitr,
            float C,
            double eta,
            unsigned short shrinking_size,
            unsigned short thread_num,
            bool orthant) {
  double old_obj = 1e+37;
  int    converge = 0;
  LBFGS lbfgs;

	// 创建线程池
	std::vector<CRFEncoderThread> thread(thread_num);
  for (size_t i = 0; i < thread_num; i++) {
    thread[i].start_i = i; // 本线程的线程号
    thread[i].size = x.size();  // 总的tagger 数量
    thread[i].thread_num = thread_num;
	  //把线程的任务队列指向：  把tagger vector 的起点
	  thread[i].x = const_cast<TaggerImpl **>(&x[0]);
    thread[i].expected.resize(feature_index->size());
  }

  size_t all = 0;  // 全部的训练文件的解析结果数
  for (size_t i = 0; i < x.size(); ++i) {
	  //x[i]->size()=> 每个tagger 的 训练解析结果数：len（[[the, DT, B], [we, DT, N],.. ..]）
    all += x[i]->size();
  }

  for (size_t itr = 0; itr < maxitr; ++itr) { // 在最大迭代次数下进行这些计算

    for (size_t i = 0; i < thread_num; ++i) {
      thread[i].start();  // 分别启动每个线程的run方法
    }

    for (size_t i = 0; i < thread_num; ++i) {
      thread[i].join();  // 等待这些线程结束
    }

    for (size_t i = 1; i < thread_num; ++i) {
      thread[0].obj += thread[i].obj;
      thread[0].err += thread[i].err;
      thread[0].zeroone += thread[i].zeroone;
    }

    for (size_t i = 1; i < thread_num; ++i) {
      for (size_t k = 0; k < feature_index->size(); ++k) {
        thread[0].expected[k] += thread[i].expected[k];
      }
    }

    size_t num_nonzero = 0;
    if (orthant) {   // L1
      for (size_t k = 0; k < feature_index->size(); ++k) {
        thread[0].obj += std::abs(alpha[k] / C);
        if (alpha[k] != 0.0) {
          ++num_nonzero;
        }
      }
    } else {  // L2
      num_nonzero = feature_index->size();
      // 请看L2 损失的公式
      for (size_t k = 0; k < feature_index->size(); ++k) {
        thread[0].obj += (alpha[k] * alpha[k] /(2.0 * C));
        thread[0].expected[k] += alpha[k] / C;
      }
    }

    double diff = (itr == 0 ? 1.0 :
                   std::abs(old_obj - thread[0].obj)/old_obj);
    std::cout << "iter="  << itr  // 当前的迭代次数
              << " terr=" << 1.0 * thread[0].err / all
              << " serr=" << 1.0 * thread[0].zeroone / x.size()
              << " act=" << num_nonzero
              << " obj=" << thread[0].obj
              << " diff="  << diff << std::endl;
    old_obj = thread[0].obj;

    if (diff < eta) {
      converge++;
    } else {
      converge = 0;
    }

    if (itr > maxitr || converge == 3) {
      break;  // 3 is ad-hoc
    }
    // 现在有了： 损失函数f(w)和梯度函数表示 ∂f(w)/∂w=g(w)
    // 直接调用拟牛顿法求这个函数的极小值，来计算这些参数,然后保存在alpha列表里面
    if (lbfgs.optimize(feature_index->size(),  // 最大特征数，就是变量数
                       &alpha[0],
                       thread[0].obj, //
                       &thread[0].expected[0], orthant, C) <= 0) {
      return false;
    }
  }

  return true;
}

bool Encoder::convert(const char* textfilename,
                      const char *binaryfilename) {
  EncoderFeatureIndex feature_index;
  CHECK_FALSE(feature_index.convert(textfilename, binaryfilename))
      << feature_index.what();

  return true;
}

bool Encoder::learn(const char *templfile, // 模板文件
                    const char *trainfile,  // 训练文件
                    const char *modelfile,  // 模型文件
                    bool textmodelfile,
                    size_t maxitr, // 最大迭代次数
                    size_t freq, // 最低词频
                    double eta,  // 迭代的收敛标准:参数变化太小就收敛  y2-y1 < etc
                    double C,  // 压缩模型吗？
                    unsigned short thread_num, // 线程数
                    unsigned short shrinking_size,
                    int algorithm) {
  std::cout << COPYRIGHT << std::endl;    // 版权字符串

	// 参数检查
  CHECK_FALSE(eta > 0.0) << "eta must be > 0.0";
  CHECK_FALSE(C >= 0.0) << "C must be >= 0.0";
  CHECK_FALSE(shrinking_size >= 1) << "shrinking-size must be >= 1";
  CHECK_FALSE(thread_num > 0) << "thread must be > 0";

#ifndef CRFPP_USE_THREAD
  CHECK_FALSE(thread_num == 1)
      << "This architecture doesn't support multi-thrading";
#endif

  if (algorithm == MIRA && thread_num > 1) {
    std::cerr <<  "MIRA doesn't support multi-thrading. use thread_num=1"
              << std::endl;
  }

  EncoderFeatureIndex feature_index;  // 这应该是模板解析类
  Allocator allocator(thread_num);  // 创建内存管理器
  std::vector<TaggerImpl* > x;  // 句子处理器 列表
	// train.data中的是每行一个字，然后一句话需要多行。 连接拼在一起，然后用空格分隔每个句子

  std::cout.setf(std::ios::fixed, std::ios::floatfield);
  std::cout.precision(5);  // 设置数值显示精度

	// 确保 清空x
#define WHAT_ERROR(msg) do {                                    \
    for (std::vector<TaggerImpl *>::iterator it = x.begin();    \
         it != x.end(); ++it)                                   \
      delete *it;                                               \
    std::cerr << msg << std::endl;                              \
    return false; } while (0)

	// 解析模板文件  读取训练文件的 状态标记 集合
  CHECK_FALSE(feature_index.open(templfile, trainfile))
      << feature_index.what();

  {
    progress_timer pg;

	  // 拿到文件句柄
    std::ifstream ifs(WPATH(trainfile));
    CHECK_FALSE(ifs) << "cannot open: " << trainfile;

    std::cout << "reading training data: " << std::flush;
    size_t line = 0;
    while (ifs) {  // 当句柄可用(它是会偏移的，每次从上次读取到的地方开始读)
      TaggerImpl *_x = new TaggerImpl();  // 为train.data中的每个句子创建一个
      _x->open(&feature_index, &allocator); // 配置引用
      // 逐行读取 训练文本， 然后匹配模板，形成大量的特征函数，并维护到特征函数字典中
      if (!_x->read(&ifs) || !_x->shrink()) {
        WHAT_ERROR(_x->what());
      }
      if (!_x->empty()) {  // 如果尚未计算其  特征函数对应的权重list
        x.push_back(_x);   // 插入
      } else {
        delete _x;  // 计算过，就丢弃
        continue;
      }

      _x->set_thread_id(line % thread_num);  // 为这个句子处理器trager 分配线程号

      if (++line % 100 == 0) {  // 每100行打印一个进度条
        std::cout << line << ".. " << std::flush;
      }
    }

    ifs.close();
    std::cout << "\nDone!";
  }
	// 数据准备工作结束0_0

	// 从特征函数字典中删除那些 出现频次 < freq 的特征函数
  feature_index.shrink(freq, &allocator);

  std::vector <double> alpha(feature_index.size());  // 特征函数的权重参数列表
  std::fill(alpha.begin(), alpha.end(), 0.0);
  feature_index.set_alpha(&alpha[0]);  // 把特征函数的权重全部设置成 0

  std::cout << "Number of sentences: " << x.size() << std::endl;
  std::cout << "Number of features:  " << feature_index.size() << std::endl;
  std::cout << "Number of thread(s): " << thread_num << std::endl;
  std::cout << "Freq:                " << freq << std::endl;
  std::cout << "eta:                 " << eta << std::endl;
  std::cout << "C:                   " << C << std::endl;
  std::cout << "shrinking size:      " << shrinking_size
            << std::endl;

  progress_timer pg;

	// 现在：
	// x: 句子集合
	// feature_index: 整体的所有特征函数都收集在这里
	// alpha : 特征函数的权重列表
  switch (algorithm) {  // 这是真正的执行单元，根据不同的参数，执行CRF核心
    case MIRA:
      if (!runMIRA(x, &feature_index, &alpha[0],
                   maxitr, C, eta, shrinking_size, thread_num)) {
        WHAT_ERROR("MIRA execute error");
      }
      break;
    case CRF_L2:  // 以此为例
      if (!runCRF(x, &feature_index, &alpha[0],
                  maxitr, C, eta, shrinking_size, thread_num, false)) {
        WHAT_ERROR("CRF_L2 execute error");
      }
      break;
    case CRF_L1:
      if (!runCRF(x, &feature_index, &alpha[0],
                  maxitr, C, eta, shrinking_size, thread_num, true)) {
        WHAT_ERROR("CRF_L1 execute error");
      }
      break;
  }

  for (std::vector<TaggerImpl *>::iterator it = x.begin();
       it != x.end(); ++it) {
    delete *it;
  }

  if (!feature_index.save(modelfile, textmodelfile)) {
    WHAT_ERROR(feature_index.what());
  }

  std::cout << "\nDone!";

  return true;
}

namespace {
const CRFPP::Option long_options[] = {  // 可配置的输入参数列表
  // 特征最低频次
  {"freq",     'f', "1",      "INT",
   "use features that occuer no less than INT(default 1)" },
  // 最大迭代次数
  {"maxiter" , 'm', "100000", "INT",
   "set INT for max iterations in LBFGS routine(default 10k)" },
  {"cost",     'c', "1.0",    "FLOAT",  // 表示拟合的程度，可以用来调节过拟合的质量
   "set FLOAT for cost parameter(default 1.0)" },
  // 收敛阈值
  {"eta",      'e', "0.0001", "FLOAT",
   "set FLOAT for termination criterion(default 0.0001)" },
  {"convert",  'C',  0,       0, // 压缩model成二进制文件
   "convert text model to binary model" },
  // 是否输出文本形式的模型文件
  {"textmodel", 't', 0,       0,
   "build also text model file for debugging" },
  // 训练算法
  {"algorithm",  'a', "CRF",   "(CRF|MIRA)", "select training algorithm" },
  {"thread", 'p',   "0",       "INT",
   "number of threads (default auto-detect)" },
  {"shrinking-size", 'H', "20", "INT",
   "set INT for number of iterations variable needs to "
   " be optimal before considered for shrinking. (default 20)" },
  {"version",  'v', 0,        0,       "show the version and exit" },
  {"help",     'h', 0,        0,       "show this help and exit" },
  {0, 0, 0, 0, 0}
};

int crfpp_learn(const Param &param) {
    // 解析参数，然后调用 实际的CRF算法
  if (!param.help_version()) {
    return 0;
  }

  const bool convert = param.get<bool>("convert"); //是否压缩model

  const std::vector<std::string> &rest = param.rest_args();  // 输入参数列表
  if (param.get<bool>("help") ||  // 检查参数的个数
      (convert && rest.size() != 2) || (!convert && rest.size() != 3)) {
    std::cout << param.help();
    return 0;
  }

  const size_t         freq           = param.get<int>("freq");
  const size_t         maxiter        = param.get<int>("maxiter");
  const double         C              = param.get<float>("cost");
  const double         eta            = param.get<float>("eta");
  const bool           textmodel      = param.get<bool>("textmodel");
  const unsigned short thread         =  // 线程数
      CRFPP::getThreadSize(param.get<unsigned short>("thread"));
  const unsigned short shrinking_size
      = param.get<unsigned short>("shrinking-size");
  std::string salgo = param.get<std::string>("algorithm");  // 训练算法

  CRFPP::toLower(&salgo);

    //根据参数选择训练算法
  int algorithm = CRFPP::Encoder::MIRA;
  if (salgo == "crf" || salgo == "crf-l2") {
    algorithm = CRFPP::Encoder::CRF_L2;
  } else if (salgo == "crf-l1") {
    algorithm = CRFPP::Encoder::CRF_L1;
  } else if (salgo == "mira") {
    algorithm = CRFPP::Encoder::MIRA;
  } else {
    std::cerr << "unknown alogrithm: " << salgo << std::endl;
    return -1;
  }

  CRFPP::Encoder encoder;
  if (convert) {  // 现在不支持压缩,命令行选中这个参数就会报错
    if (!encoder.convert(rest[0].c_str(), rest[1].c_str())) {
      std::cerr << encoder.what() << std::endl;
      return -1;
    }
  } else {
      // 执行真正的而训练过程
      // 各种参数转成字符串
    if (!encoder.learn(rest[0].c_str(),  // 模板文件
                       rest[1].c_str(),  // 训练语料
                       rest[2].c_str(),  // 模型的输出文件
                       // 下面是命令的控制参数
                       textmodel,
                       maxiter, freq, eta, C, thread, shrinking_size,
                       algorithm)) {
      std::cerr << encoder.what() << std::endl;
      return -1;
    }
  }

  return 0;
}
}  // namespace
}  // CRFPP

int crfpp_learn2(const char *argv) {
  CRFPP::Param param;
  param.open(argv, CRFPP::long_options);
  return CRFPP::crfpp_learn(param);
}

int crfpp_learn(int argc, char **argv) {
  std::cout << "开始运行crfpp_learn!!!" << std::endl; // 这是我添加的第一行程序
  CRFPP::Param param; //运行参数
  param.open(argc, argv, CRFPP::long_options); // 命令行的参数解析
  return CRFPP::crfpp_learn(param);
}

