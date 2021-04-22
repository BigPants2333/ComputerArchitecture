#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "cachelab.h"

/* structure define */
typedef struct line {
    int count;      // LRU算法替换依据, hit减1, 其余情况自增1
    char valid;     // 有效位
    int tag;        // 组号
    int* block;     // 块数据
} line;             // cache行定义

typedef struct set {
    line* lines;    // 一个组内的多个行
} set;            	// cache组定义

typedef struct cache {
    set* sets;  	// 一个cache内有多个组
} cache;            // cache定义

/* main function define */

void help();												// 打印帮助文档
line* createLine(int tag, int blockSize, int memAddr);		// 创建行
cache* createCache(int setNum, int lineNum);				// 创建Cache
void freeCache(cache* c, int setNum, int lineNum);			// 销毁Cache
void countIncrement(set* curSet, int lineNum);				// count自增1
int calculateAddr(char* fileLine, long* memAddr);			// 计算要访问的主存地址及字节长度
void recordInfo(set* curSet, int lineNum, int tag, int blockSize, long memAddr,
				int* hits, int* misses, int* evictions);	// 记算并记录 hit, miss, eviction 的次数
void recordVerbose(int* oldHits, int* oldMisses, int* oldEvictions,
				   int* hits, int* misses, int* evictions);	// 详细记录 hit, miss, eviction 信息
void runCache(int v, int s, int E, int b, FILE* file,
			  int* hits, int* misses, int* evictions);		// 运行模拟Cache

/**
 * @brief main函数入口
 * 
 * @param argc 参数个数
 * @param argv 参数字符串
 * @return int 结束返回0, 无异常
 */
int main(int argc, char** argv) {
	if(argc == 1) {
		printf("./csim: Missing required command line argument\n");
		help();
		return 1;
	}
    int v = 0;
    int s, E, b, i;
    FILE* file;

    for(i = 1; i < argc; ++i) {				// 处理参数
        char* arg = *(argv + i);			// 获取当前参数
		char* nextArg = *(argv + i + 1);	// 获取下一参数
		if(strcmp(arg, "-h") == 0) {		// 帮助文档
			help();
		}
		else if(strcmp(arg, "-v") == 0) {	// 可选冗余标志
			v = 1;
		}
		else if(strcmp(arg, "-s") == 0) {	// 设置索引位数
			s = atoi(nextArg);
			++i;
		}
		else if(strcmp(arg, "-E") == 0) {	// 设置每组的行数
			E = atoi(nextArg);
			++i;
		}
		else if(strcmp(arg, "-b") == 0) {	// 块偏移位数
			b = atoi(nextArg);
			++i;
		}
		else if(strcmp(arg, "-t") == 0) {	// 文件
			file = fopen(nextArg, "r");
			++i;
		}
    }

	int hits, misses, evictions;
	runCache(v, s, E, b, file, &hits, &misses, &evictions);
	printSummary(hits, misses, evictions);	// cachelab.h 提供的辅助函数

	return 0;
}

/* process function define */

/**
 * @brief 打印帮助文档
 * 
 */
void help() {
    printf(
        "Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t <file>\n"
        "Options:\n"
        "\t-h\t\tPrint this help message.\n"
        "\t-v\t\tOptional verbose flag.\n"
        "\t-s <num>\tNumber of set index bits.\n"
        "\t-E <num>\tNumber of lines per set.\n"
        "\t-b <num>\tNumber of block offset bits.\n"
        "\t-t <file>\tTrace file.\n"
        "\n"
        "Examples:\n"
        "\tlinux>  ./csim -s 4 -E 1 -b 4 -t traces/yi.trace\n"
        "\tlinux>  ./csim -v -s 8 -E 2 -b 4 -t traces/yi.trace\n"
        );
}

/**
 * @brief Create a Line object
 * 
 * @param tag 组号
 * @param blockSize 块大小
 * @param memAddr 内存地址
 * @return line* 创建的行指针
 */
line* createLine(int tag, int blockSize, int memAddr) {
	line* newLine = (line *)malloc(sizeof(line));
	newLine->valid = 1;
	newLine->count = -1;
	newLine->tag = tag;
	int *newBlock = (int *)malloc(blockSize * sizeof(int));
	int byteNum = memAddr & (blockSize - 1);
	for(int i = 0; i < blockSize; ++i) {
		newBlock[i] = memAddr - byteNum + i;
	}
	newLine->block = newBlock;
	return newLine;
}

/**
 * @brief Create a Cache object
 * 
 * @param setNum 组数
 * @param lineNum 行数
 * @return cache* 创建的Cache指针
 */
cache* createCache(int setNum, int lineNum) {
	cache* newCache = (cache *)malloc(sizeof(cache));
	set* newSets = (set *)malloc(setNum * sizeof(set));
	newCache->sets = newSets;
	for(int i = 0; i < setNum; ++i) {
		line* newLines = (line *)malloc(lineNum * sizeof(line));
		for(int j = 0; j < lineNum; ++j) {
			newLines[j].valid = 0;
		}
		newSets[i].lines = newLines;
	}
	return newCache;
}

/**
 * @brief 释放Cache空间
 * 
 * @param c 要释放的Cache
 * @param sets 组数
 * @param lines 行数
 */
void freeCache(cache* c, int setNum, int lineNum) {
	for(int i = 0; i < setNum; ++i) {
		for(int j = 0; i < lineNum; ++j) {
			free(c->sets[i].lines[j].block);
		}
		free(c->sets[i].lines);
	}
	free(c->sets);
	free(c);
}

/**
 * @brief 将此轮没有命中的行的count加1
 * 		  用于LRU算法对数据的更新
 * @param curSet 当前组
 * @param lineNum 行数
 */
void countIncrement(set* curSet, int lineNum) {
	for(int i = 0; i < lineNum; ++i) {
		if(curSet->lines[i].valid == 1) {
			curSet->lines[i].count += 1;
		}
	}
}

/**
 * @brief 计算要访问的主存地址及字节长度
 * 
 * @param fileLine 文件的一行内容
 * @param memAddr 主存地址
 * @return int 字节长度
 */
int calculateAddr(char* fileLine, long* memAddr) {
	int addrLen = 0;
	while(fileLine[3 + addrLen] != ',') {
		++addrLen;
	}
	char* addr = (char *)malloc(addrLen * sizeof(char));
	int i = 0;
	do {
		addr[i] = fileLine[i + 3];
		++i;
	} while(i < addrLen);
	*(addr + addrLen) = '\0';	// 拼接字符串
	*memAddr = strtol(addr, NULL, 16);
	free(addr);
	return addrLen;
}

/**
 * @brief 记算并记录 hit, miss, eviction 的次数
 * 
 * @param curSet 当前组
 * @param lineNum 行数
 * @param tag 组号
 * @param blockSize 块大小
 * @param memAddr 内存地址
 * @param hits hit次数
 * @param misses miss次数
 * @param evictions eviction次数
 */
void recordInfo(set* curSet, int lineNum, int tag, int blockSize, long memAddr,
				int* hits, int* misses, int* evictions) {
	int invalidLine = -1;
	int evictionLine = -1;
	int maxCount = 0;
	for(int i = 0; i < lineNum; ++i) {			// 多路选择器
		if((curSet->lines[i].valid == 1) && (curSet->lines[i].tag) == tag) {
			*hits += 1;							// hit
			curSet->lines[i].count = -1;
			countIncrement(curSet, lineNum);	// 该组中其他行的count自增1
			return;
		}
		else if((invalidLine == -1) && (curSet->lines[i].valid == 0)) {
			invalidLine = i;					// 记录没被使用的line
		}
		else if((curSet->lines[i].valid == 1) && (curSet->lines[i].count >= maxCount)) {
			evictionLine = i;					// 记录当前需要被替换的行
			maxCount = curSet->lines[i].count;
		}
	}
	*misses += 1;				// miss
	if(invalidLine != -1) {		// 先选择填满未使用的行
		curSet->lines[invalidLine] = *createLine(tag, blockSize, memAddr);
		countIncrement(curSet, lineNum);
	}
	else {						// 替换count最大的行
		*evictions += 1;		// eviction
		free(curSet->lines[evictionLine].block);
		curSet->lines[evictionLine] = *createLine(tag, blockSize, memAddr);
		countIncrement(curSet, lineNum);
	}
}

/**
 * @brief 详细记录 hit, miss, eviction 信息
 * 
 * @param oldHits 上一次hit信息
 * @param oldMisses 上一次miss信息
 * @param oldEvictions 上一次eviction信息
 */
void recordVerbose(int* oldHits, int* oldMisses, int* oldEvictions,
				   int* hits, int* misses, int* evictions) {
	if(*misses > *oldMisses) {
		*oldMisses = *misses;
		printf(" miss");
	}
	if(*evictions > *oldEvictions) {
		*oldEvictions = *evictions;
		printf(" eviction");
	}
	if(*hits > *oldHits) {
		*oldHits = *hits;
		printf(" hit");
	}
}

/**
 * @brief 运行模拟Cache
 * 
 * @param v 可选冗余标志
 * @param s 索引位数
 * @param E 每组的行数
 * @param b 块偏移位数
 * @param file 文件
 * @param hits hit的次数
 * @param misses miss的次数
 * @param evictions eviction的次数
 */
void runCache(int v, int s, int E, int b, FILE* file,
			  int* hits, int* misses, int* evictions) {
	*hits = 0;					// 上一次hit数
	*misses = 0;				// 上一次miss数
	*evictions = 0;				// 上一次eviction数

	int setNum = pow(2, s);		// 组数
	int blockSize = pow(2, b);	// 块大小
	cache* simulationCache = createCache(setNum, E);	// 模拟Cache

	char* fileLine = NULL;		// 读取文件内容的行
	size_t length = 0;			// 每一个line的长度
	ssize_t read;				// 读取的内容数

	if(v == 1) {				// 可选冗余标志有效
		int oldHits = *hits;
		int oldMisses = *misses;
		int oldEvictions = *evictions;
		while((read = getline(&fileLine, &length, file)) != -1) {		// 逐行解析文件
			char* printLine = fileLine;
			printLine[strlen(printLine) - 1] = '\0';
			printf("%s", printLine);
			if(fileLine[0] == 'I') {
				continue;
			}

			long memAddr = 0;									// 主存地址
			int addrLen = calculateAddr(fileLine, &memAddr);	// 字节长度
			int setNo = (memAddr >> b) & (setNum - 1);			// 组编号
			set* curSet = &(simulationCache->sets[setNo]);		// 当前组
			int maxTag = pow(2, (4 * addrLen) - s - b) - 1;		// 最大组号
			int tag = (memAddr >> (s + b)) & maxTag;			// 组号
			char acceptType = fileLine[1];						// 类型
			if(acceptType == 'M') {								// 两次访存
				recordInfo(curSet, E, tag, blockSize, memAddr, hits, misses, evictions);
				recordVerbose(&oldHits, &oldMisses, &oldEvictions, hits, misses, evictions);
				recordInfo(curSet, E, tag, blockSize, memAddr, hits, misses, evictions);
				recordVerbose(&oldHits, &oldMisses, &oldEvictions, hits, misses, evictions);
			}
			else if(acceptType == 'S' || acceptType == 'L') {	// 一次访存
				recordInfo(curSet, E, tag, blockSize, memAddr, hits, misses, evictions);
				recordVerbose(&oldHits, &oldMisses, &oldEvictions, hits, misses, evictions);
			}
			else {
				printf("Error!");
			}
			printf("\n");
		}
	}
	else {
		while((read = getline(&fileLine, &length, file)) != -1) {		// 逐行解析文件
			if(fileLine[0] == 'I') {
				continue;
			}

			long memAddr = 0;									// 主存地址
			int addrLen = calculateAddr(fileLine, &memAddr);	// 字节长度
			int setNo = (memAddr >> b) & (setNum - 1);			// 组编号
			set* curSet = &(simulationCache->sets[setNo]);		// 当前组
			int maxTag = pow(2, (4 * addrLen) - s - b) - 1;		// 最大组号
			int tag = (memAddr >> (s + b)) & maxTag;			// 组号
			char acceptType = fileLine[1];						// 类型
			if(acceptType == 'M') {								// 两次访存
				recordInfo(curSet, E, tag, blockSize, memAddr, hits, misses, evictions);
				recordInfo(curSet, E, tag, blockSize, memAddr, hits, misses, evictions);
			}
			else if(acceptType == 'S' || acceptType == 'L') {	// 一次访存
				recordInfo(curSet, E, tag, blockSize, memAddr, hits, misses, evictions);
			}
			else {
				printf("Error!");
			}
		}
	}
	fclose(file);
	free(fileLine);
	freeCache(simulationCache, setNum, E);
}