/*
 * cluster.hpp
 *
 *  Created on: 20 Feb 2019
 *      Author: oruqimaru
 */

#pragma once

#include <omp.h>

#include <float.h>
#include <assert.h>
#include <cmath>
#include <vector>
#include <iostream>
#include <string>
#include <fstream>
#include <string>
#include <queue>
#include <utility>

#include "matrix.hpp"
#include "metric.hpp"
#include "utils/calculator.hpp"
#include "hungarian/matrix.h"  /* still not sure whether this will help */
#include "hungarian/munkres.h"
#include "hungarian/adapters/boostmatrixadapter.h"

#define NOTASSIGNED -0x978
#define NOTKNOWN -0x246

using std::cin;
using std::cout;
using std::vector;
using std::endl;


namespace sm{

	class Cluster;
	class Point;

	class Point{
		int _dimension;
		float *_data;
		int _clusterId;
		int _index;

	public:
		Point(int index, int dimension): _dimension(dimension),
			_index(index), _clusterId(NOTASSIGNED), _data(NULL){
		}

		float operator [] (int i) const {
			if (i >= _dimension){
				cout << "Point error: data access out of order" << endl;
				assert(0);
			}
			return *(_data + i);/// i * dimension may overflow int
		}

		float * get_data(){
			return _data;
		}

		void set_data(const float* data){
			_data = data;
		}

		void set_cluster(int clusterId){
			_clusterId = clusterId;
		}

		int get_dimension(){
			return _dimension;
		}

		int get_index(){
			return _index;
		}

		float L2_dist (vector<float> * centre){
			return ss::EuclidDistance_Ary2Vec(_data, centre, _dimension);
		}

		void assign_cluster (vector<Cluster*> *clusters){
			int label = -1;
			float dister = FLT_MAX;
			int size = clusters->size();
			for (int i = 0; i < size; i++){
				float dist = L2_dist (clusters->at(i)->get_centroid());
				if (dist < dister){
					label = i;
					dister = dist;
				}
			}
			clusters->at(label)->append_point(this);
		}

	};

	class Cluster{
		vector <Point *> _datas;
		vector <float> _centroid;
		int _size;
		int _dimension;
		int _aimNPartition;
		bool _done;

		// parallel thing
		omp_lock_t _appendLock;

	public:
		vector<Cluster*> _childrens;
		explicit Cluster (int dimension, float* centre): _size(0),
			_aimNPartition(NOTKNOWN), _dimension(dimension), _done (false){
			_centroid.resize(_dimension);
			set_centroid (centre);
			omp_init_lock(&_appendLock);
		}

		Point * operator [] (int i) const {
			if (i >= _size){
				cout << "Cluster error: data access out of bound" << endl;
				assert(0);
			}
			return _datas[i];/// i * dimension may overflow int
		}

		void append_point(Point* point){
			omp_set_lock (&_appendLock);
			_datas.push_back(point);
			_size++;
			omp_unset_lock (&_appendLock);
		}

		void set_centroid (float* centre){
			for (int i = 0; i < _dimension; i++)
				_centroid[i] = centre[i];
		}

		void update_centroid(){
			for (int i = 0; i < _dimension; i++)
				_centroid[i] = 0;
			for (int i = 0; i < _size; i++){
				float * appender = _datas[i]->get_data();
				for(int j = 0; j < _dimension; j++){
					_centroid[j] += appender[j] / _size;
				}
			}
		}

		void cluster_balanced (int iteration){
			for (int i = 0; i < _aimNPartition; i++){
				Cluster buffer = new Cluster(_dimension, _datas[i]->get_data());
				_childrens.push_back(& buffer);
			}

			hun::Matrix<float> task = hun::Matrix<float>(_size, _size);

			for (int iters = 0; iters < iteration; iters++){
				for (int i = 0; i < _aimNPartition; i++)
					_childrens[i]->reset_data();

#pragma omp parallel for
				for (int i = 0; i < _size; i++){
					float dists[_aimNPartition];
					for (int j = 0; j < _aimNPartition; i++)
						dists[j] = _datas[i]->L2_dist(_childrens[j]->get_centroid());
					for (int j = 0; j < _size; j++)
						task(i, j) = dists[j % _aimNPartition];
				}

				Munkres<float> m;
				m.solve(task);

				for (int i = 0; i < _size; i++){
					for (int j = 0; j < _size; j++){
						if (task(i, j) == 0)
							_childrens[j]->append_point(_datas[i]);
					}
				}

				for (int i = 0; i < _aimNPartition; i++){
					_childrens[i]->update_centroid();
				}
			}

			for (int i = 0; i < _aimNPartition; i++){
				_childrens[i]->set_aimNPartition(1);
				_childrens[i]->set_done();
			}
		}

		void save_cluster(std::string outFile){
			std::ofstream wFile;
			wFile.open(outFile.c_str());
			for (int i = 0; i < _size; i++)
				wFile << _datas[i]->get_index() << endl;
			wFile.close();
		}

		void cluster_binary (int iteration){
			Cluster c1 = new Cluster(_dimension, _datas[0]->get_data());
			Cluster c2 = new Cluster(_dimension, _datas[1]->get_data());
			_childrens.push_back(& c1);
			_childrens.push_back(& c2);

			for (int i = 0; i < iteration; i++){
				for (int j = 0; j < _aimNPartition; j++)
					_childrens[j]->reset_data();
#pragma omp parallel for
				for(int j = 0; j < _size; j++){
					_datas[j]->assign_cluster(&_childrens);
				}

#pragma omp parallel for
				for(int j = 0; j < _childrens.size(); j++){
					_childrens[j]->update_centroid();
				}
			}

			/*
			 * 一共要分为 x 分，第一个有size1， 第二个有size2
			 *
			 */
			float numPCluster = (float)_size / (float)_aimNPartition;
			int aim1 = (int) round((float)_childrens[0]->get_size() / numPCluster);
			if (aim1 == 0)
				aim1 = 1;
			_childrens[0]->set_aimNPartition(aim1);
			if (aim1 == 1)
				_childrens[0]->set_done();
			_childrens[1]->set_aimNPartition(_aimNPartition - aim1);
			if (_aimNPartition - aim1 == 1)
				_childrens[1]->set_done();
		}

		void set_done (){
			_done = true;
		}

		void set_aimNPartition(int num){
			_aimNPartition = num;
		}

		vector<float> * get_centroid(){
			return &_centroid;
		}

		int get_size (){
			return _size;
		}

		int get_aim_partition(){
			return _aimNPartition;
		}

		int get_dimension (){
			return _dimension;
		}

		int done_or_not (){
			return _done;
		}

		void reset_data (){
			_size = 0;
			_datas.resize(0);
		}
	};

	struct cmp{
	    bool operator() ( Cluster* a , Cluster* b ){
	    	return true;      //与greater是等价的
	    }
	};

	void cluster_machine (const ss::Matrix<float>& datas,std::string dire, int nPartition, int iteration, int bomber){
		int dim = datas.getDim();
		Cluster root = new Cluster (dim, datas[0]);

		for (int i = 0; i < datas.getSize(); i++){
			Point newPoint = new Point(i, dim);
			newPoint.set_data(datas[i]);
			root.append_point(&newPoint);
		}

		root.set_aimNPartition(nPartition);
		std::priority_queue<Cluster*, std::vector<Cluster*>, cmp> workList;
		std::vector<Cluster*> readyList;
		workList.push(&root);

		while(!workList.empty()){
			Cluster & aimer = workList.pop();
			if (aimer.done_or_not())
				readyList.push_back(&aimer);

			if (aimer.get_aim_partition() > bomber)
				aimer.cluster_binary(iteration);
			else
				aimer.cluster_balanced(iteration);

			for (int i = 0; i < aimer.get_aim_partition(); i++)
				workList.push(aimer._childrens[i]);

			delete aimer;
		}

		assert (readyList.size() == nPartition);

		std::ofstream wFile;
		wFile.open(dire.c_str());

		wFile << nPartition << endl;

		for (int i = 0; i < nPartition; i++){
			wFile << i << " "<< readyList[i]->get_size() << endl;
			for (int j = 0; j < readyList[i]->get_size(); j++)
				wFile << readyList[i]->operator [](j)->get_index() << endl;
		}
	}
}


