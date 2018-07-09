//
//  CRF++ -- Yet Another CRF toolkit
//
//  $Id: feature.cpp 1595 2007-02-24 10:18:32Z taku $;
//
//  Copyright(C) 2005-2007 Taku Kudo <taku@chasen.org>
//
#include "feature_index.h"
#include "common.h"
#include "node.h"
#include "path.h"
#include "tagger.h"

namespace CRFPP {
const size_t kMaxContextSize = 8;

const char *BOS[kMaxContextSize] = { "_B-1", "_B-2", "_B-3", "_B-4",
                                     "_B-5", "_B-6", "_B-7", "_B-8" };
const char *EOS[kMaxContextSize] = { "_B+1", "_B+2", "_B+3", "_B+4",
                                     "_B+5", "_B+6", "_B+7", "_B+8" };

const char *FeatureIndex::getIndex(const char *&p,
                                   size_t pos,
                                   const TaggerImpl &tagger) const {
  if (*p++ !='[') {
    return 0;
  }

  int col = 0;
  int row = 0;

  int neg = 1;
  if (*p++ == '-') {
    neg = -1;
  } else {
    --p;
  }

  for (; *p; ++p) {
    switch (*p) {
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        row = 10 * row +(*p - '0');
        break;
      case ',':
        ++p;
        goto NEXT1;
      default: return  0;
    }
  }

NEXT1:

  for (; *p; ++p) {
    switch (*p) {
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        col = 10 * col + (*p - '0');
        break;
      case ']': goto NEXT2;
      default: return 0;
    }
  }

NEXT2:

  row *= neg;

  if (row < -static_cast<int>(kMaxContextSize) ||
      row > static_cast<int>(kMaxContextSize) ||
      col < 0 || col >= static_cast<int>(tagger.xsize())) {
    return 0;
  }

  // TODO(taku): very dirty workaround
  if (check_max_xsize_) {
    max_xsize_ = std::max(max_xsize_, static_cast<unsigned int>(col + 1));
  }

  const int idx = pos + row;
  if (idx < 0) {
    return BOS[-idx-1];
  }
  if (idx >= static_cast<int>(tagger.size())) {
    return EOS[idx - tagger.size()];
  }

  return tagger.x(idx, col);
}

// 利用传入的特征模板，对传入的一句话作用一遍，生成特征函数
bool FeatureIndex::applyRule(string_buffer *os,
                             const char *p,
                             size_t pos,
                             const TaggerImpl& tagger) const {
  os->assign("");  // clear
  const char *r;

  for (; *p; p++) {
    switch (*p) {
      default:
        *os << *p;
        break;
      case '%':
        switch (*++p) {
          case 'x':
            ++p;
            r = getIndex(p, pos, tagger);
            if (!r) {
              return false;
            }
            *os << r;
            break;
          default:
            return false;
        }
        break;
    }
  }

  *os << '\0';

  return true;
}

void FeatureIndex::rebuildFeatures(TaggerImpl *tagger) const {
	// 构建这个句子的篱笆图
  size_t fid = tagger->feature_id();
  const size_t thread_id = tagger->thread_id();

  Allocator *allocator = tagger->allocator();
  allocator->clear_freelist(thread_id);

	// 这个句子对应的  "字"特征函数集 组成的集合
  FeatureCache *feature_cache = allocator->feature_cache();

	// 对这个tagger 中的行进行逐个解析--创建node

	// 外层循环  -- 对应 篱笆图中的x轴
  for (size_t cur = 0; cur < tagger->size(); ++cur) {
    const int *f = (*feature_cache)[fid++];  // 取出这个字对应的特征函数集合
    for (size_t i = 0; i < y_.size(); ++i) {
	    //内层循环，对应篱笆图中的y轴
      Node *n = allocator->newNode(thread_id);  // 创建 node
      n->clear();
      n->x = cur; // 当前时刻的x轴偏移
      n->y = i;  // 当前时刻对应的y轴偏移
      n->fvector = f;  // 这个x(观测值)对应的每个y值(y轴上的节点)，都拿到这个字的所有特征函数集合
      tagger->set_node(n, cur, i);  // 构建一句话的网络拓扑图
    }
  }

	// 对这个tagger 中的行进行逐个解析--创建path

	// 外层循环  -- 对应 篱笆图中的x轴
	for (size_t cur = 1; cur < tagger->size(); ++cur) {
    const int *f = (*feature_cache)[fid++];
    for (size_t j = 0; j < y_.size(); ++j) {
	    //内层循环，对应 cur-1 处 的篱笆图中的y轴

      for (size_t i = 0; i < y_.size(); ++i) {
	      //内层循环，对应 cur 处 的篱笆图中的y轴
        Path *p = allocator->newPath(thread_id);  // 创建 path
        p->clear();
	      // 添加节点
        p->add(tagger->node(cur - 1, j),
               tagger->node(cur,     i));

	      // 把这个cur(x轴) 对应的字特征函数集，绑定到这个边上，方便使用
        p->fvector = f;
      }
    }
  }
}

// 把特征函数字符串插入 特征字典中，然后返回在字典中的索引ID(pair对的第一个值)
#define ADD { const int id = getID(os.c_str());         \
    if (id != -1) feature.push_back(id); } while (0)

bool FeatureIndex::buildFeatures(TaggerImpl *tagger) const {
  // 构建特征函数，并把特征函数插入字典维护
  string_buffer os;
  std::vector<int> feature;  // 存放的是本tagger(句子)，所生成的所有特征函数的索引ID

  FeatureCache *feature_cache = tagger->allocator()->feature_cache();
  tagger->set_feature_id(feature_cache->size());  // 设置当前的feature id

	// 应用U类模板 创建特征函数
	// 对tagger(一句话)的每个训练行[the, DT, B]这样的，经历一遍所有的 模板行
	std::cout<< "---------------" << std::endl;
  for (size_t cur = 0; cur < tagger->size(); ++cur) {
    for (std::vector<std::string>::const_iterator it
             = unigram_templs_.begin();
         it != unigram_templs_.end(); ++it) {
	    // it->c_str(), 一个个的模板   c_str：把常规字符串，转变成以空字符结尾的标准字符串
      if (!applyRule(&os, it->c_str(), cur, *tagger)) {
        return false;
      }

	    std::cout<< "本句话" << cur << "第个字(观测值)" << ": " << *tagger->x(cur) << "   " <<
					    "所用模板是" << it->c_str()  << "   os应用模板后的特征是: " << os << std::endl;
      ADD;
    }
    feature_cache->add(feature);
    feature.clear();
  }
//	for (std::vector<std::string>::const_iterator
//					     it = bigram_templs_.begin();
//	     it != bigram_templs_.end(); ++it) {
//		std::cout <<"@@U" << *it << std::endl;
//
//	}
	// 应用B类模板创建  特征函数
  for (size_t cur = 1; cur < tagger->size(); ++cur) {
    for (std::vector<std::string>::const_iterator
             it = bigram_templs_.begin();
         it != bigram_templs_.end(); ++it) {
      if (!applyRule(&os, it->c_str(), cur, *tagger)) {
        return false;
      }
      ADD;
    }
    feature_cache->add(feature);
    feature.clear();
  }
//	for (std::vector<std::string>::const_iterator
//					     it = bigram_templs_.begin();
//	     it != bigram_templs_.end(); ++it) {
//		std::cout <<"@@U" << *it << std::endl;
//
//	}

  return true;
}
#undef ADD
}
