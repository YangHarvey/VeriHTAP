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

#include "global_struct.h"
#include "helper.h"
#include "thread.h"
#include "log_thread.h"
#include "logger.h"
#include "wl.h"

void LogThread::setup() {}

RC LogThread::run() {
  tsetup();

  // pthread_barrier_wait(&log_bar);
  if (g_log_recover) {
    while (true)
		{ //glob_manager->get_workload()->sim_done < g_thread_cnt) {
			uint64_t bytes = logger->tryReadLog();
			// total_log_data += bytes;
			if (logger->iseof())
				break;
			if (bytes == 0)
			{
				usleep(100);
			}
      // logger.flushBufferCheck();
      // if (_wl->sim_done) {
      //   // logger.~Logger();
      //   return FINISH;
      // }
		}
  } else {
    while (true) {
      // logger.processRecord(get_thd_id());
      uint32_t bytes = (uint32_t)logger->tryFlush();

      // logger.flushBufferCheck();
      if (_wl->sim_done) {
        // logger.~Logger();
        return FINISH;
      }
    }
  }


  return FINISH;

}


