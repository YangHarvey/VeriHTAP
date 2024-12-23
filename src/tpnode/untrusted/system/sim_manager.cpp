/*
	 Copyright 2016 Massachusetts Institute of Technology

	 Licensed under the Apache License, Version 2.0 (the "License");
	 you may not use this file except in compliance with the License.
	 You may obtain a copy of the License at

			 http://www.apache.org/licenses/LICENSE-2.0

	 Unless required by applicable law or agreed to in writing, software
	 distributed under the License is distributed on an "AS IS" BASIS,
	 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	 See the License for the specific language governing permissions and
	 limitations under the License.
*/

#include "global.h"
#include "helper.h"
#include "sim_manager.h"

void SimManager::init() {
	sim_done = false;
	warmup = false;
	warmup_end_time = 0;
	start_set = false;
	sim_init_done = false;
	txn_cnt = 0;
	inflight_cnt = 0;
	epoch_txn_cnt = 0;
	worker_epoch = 1;
	seq_epoch = 0;
	rsp_cnt = NODE_CNT - 1;

#if TIME_ENABLE
	run_starttime = get_sys_clock();
	last_da_query_time = get_sys_clock();
#else
	run_starttime = get_wall_clock();
#endif
	last_worker_epoch_time = run_starttime;
	// last_seq_epoch_time = get_wall_clock();
}

void SimManager::set_starttime(uint64_t starttime) {
	if(ATOM_CAS(start_set, false, true)) {
		run_starttime = starttime;
		last_da_query_time = starttime;
		last_worker_epoch_time = starttime;
		sim_done = false;
		printf("Starttime set to %ld\n",run_starttime);
	} 
}
bool SimManager::timeout() {
// #if TIME_ENABLE
// 	return (get_sys_clock() - run_starttime) >= g_done_timer + g_warmup_timer;
// #else
// 	return (get_wall_clock() - run_starttime) >= g_done_timer + g_warmup_timer;
// #endif
}

bool SimManager::is_done() {
	bool done = sim_done || timeout();
	if(done && !sim_done) {
		set_done();
	}
	return done;
}

bool SimManager::is_warmup_done() {
	// if(warmup)
	// 	return true;
	// bool done = ((get_sys_clock() - run_starttime) >= g_warmup_timer);
	// if(done) {
	// 	ATOM_CAS(warmup_end_time,0,get_sys_clock());
	// 	ATOM_CAS(warmup,false,true);
	// }
	// return done;
}
bool SimManager::is_setup_done() { return sim_init_done; }

void SimManager::set_setup_done() { ATOM_CAS(sim_init_done, false, true); }

void SimManager::set_done() {
	if(ATOM_CAS(sim_done, false, true)) {
		if (warmup_end_time == 0) warmup_end_time = run_starttime;
	}
	printf("set done\n");
	fflush(stdout);
}

void SimManager::process_setup_msg() {
	uint64_t rsp_left = ATOM_SUB_FETCH(rsp_cnt,1);
	if(rsp_left == 0) {
		set_setup_done();
	}
}
