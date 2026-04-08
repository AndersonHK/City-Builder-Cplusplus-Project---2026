#pragma once

#include <atomic>
using namespace std;

//class lot;

class square {
public:
	atomic<long int> landValue = 160000;
	atomic<long int> airPolution = 0;
	atomic<bool> isVacant = true;
	atomic<short int> zoningType = 0;
	atomic<int> x;
	atomic<int> y;
//	atomic<lot*> lotBuilt;
	void Update(long int NlandValue, long int NairPolution, bool NisVacant, short int NzoningType);
	void localUpdate();
};