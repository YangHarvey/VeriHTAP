// #include <mm_malloc.h>
#include "global_enc_struct.h"
#include "table.h"
#include "catalog.h"
#include "row_enc.h"
// #include "txn.h"
#include "row_lock.h"
#include "row_ts.h"
#include "row_mvcc.h"
#include "row_hekaton.h"
#include "row_occ.h"
#include "row_tictoc.h"
#include "row_silo.h"
#include "mem_helper_enc.h"
#include "coder.h"
// #include "row_vll.h"
// #include "common/mem_alloc.h"
// #include "manager_enc.h"

RC 
row_t::init(table_t * host_table, uint64_t part_id, uint64_t row_id) {
	_row_id = row_id;
	_part_id = part_id;
	this->table = host_table;
	Catalog * schema = host_table->get_schema();
	int tuple_size = schema->get_tuple_size();
	// data = (char *) aligned_alloc(64, sizeof(char) * tuple_size);
	data = (char *) malloc(sizeof(char) * tuple_size);
	return RCOK;
}
void 
row_t::init(int size) 
{
	// data = (char *) aligned_alloc(64, size);
	data = (char *) malloc(size);
}

RC 
row_t::switch_schema(table_t * host_table) {
	this->table = host_table;
	return RCOK;
}

void row_t::init_manager(row_t * row) {
#if CC_ALG == DL_DETECT || CC_ALG == NO_WAIT || CC_ALG == WAIT_DIE
    manager = (Row_lock *) malloc(sizeof(Row_lock));
#elif CC_ALG == TIMESTAMP
    manager = (Row_ts *) malloc(sizeof(Row_ts));
#elif CC_ALG == MVCC
    manager = (Row_mvcc *) malloc(sizeof(Row_mvcc));
#elif CC_ALG == HEKATON
    manager = (Row_hekaton *) malloc(sizeof(Row_hekaton));
#elif CC_ALG == OCC
    manager = (Row_occ *) malloc(sizeof(Row_occ));
#elif CC_ALG == TICTOC
	manager = (Row_tictoc *) malloc(sizeof(Row_tictoc));
#elif CC_ALG == SILO
	manager = (Row_silo *) malloc(sizeof(Row_silo));
#endif

#if CC_ALG != HSTORE
	manager->init(this);
#endif
}

table_t * row_t::get_table() { 
	return table; 
}

Catalog * row_t::get_schema() { 
	return get_table()->get_schema(); 
}

const char* row_t::get_table_name() {
	return get_table()->get_table_name(); 
};
uint64_t row_t::get_tuple_size() {
	return table->get_schema()->get_tuple_size();
}

uint64_t row_t::get_field_cnt() { 
	return get_schema()->field_cnt;
}

void row_t::set_value(int id, void * ptr) {
	uint64_t datasize = get_schema()->get_field_size(id);
	uint64_t pos = get_schema()->get_field_index(id);
	memcpy( &data[pos], ptr, datasize);
}

void row_t::set_value(int id, void * ptr, int size) {
	int pos = get_schema()->get_field_index(id);
	memcpy( &data[pos], ptr, size);
}

void row_t::set_value(const char * col_name, void * ptr) {
	uint64_t id = get_schema()->get_field_id(col_name);
	set_value(id, ptr);
}

SET_VALUE(uint64_t);
SET_VALUE(int64_t);
SET_VALUE(double);
SET_VALUE(UInt32);
SET_VALUE(SInt32);

GET_VALUE(uint64_t);
GET_VALUE(int64_t);
GET_VALUE(double);
GET_VALUE(UInt32);
GET_VALUE(SInt32);

char * row_t::get_value(int id) {
	uint64_t pos = get_schema()->get_field_index(id);
	return &data[pos];
}

char * row_t::get_value(char * col_name) {
	uint64_t pos = get_schema()->get_field_index(col_name);
	return &data[pos];
}

void row_t::set_row_id(uint64_t id) {
    _row_id = id;
}

char * row_t::get_data() { return data; }

void row_t::set_data(char * data, uint64_t size) { 
	memcpy(this->data, data, size);
}
// copy from the src to this
void row_t::copy(row_t * src) {
	set_data(src->get_data(), src->get_tuple_size());
}

void row_t::free_row() {
	free(data);
}

RC row_t::get_row(access_t type, txn_man * txn, row_t *& row) {
	RC rc = RCOK;
#if CC_ALG == WAIT_DIE || CC_ALG == NO_WAIT || CC_ALG == DL_DETECT
	uint64_t thd_id = txn->get_thd_id();
	lock_t lt = (type == RD || type == SCAN)? LOCK_SH : LOCK_EX;
#if CC_ALG == DL_DETECT
	uint64_t * txnids;
	int txncnt; 
	rc = this->manager->lock_get(lt, txn, txnids, txncnt);	
#else
	rc = this->manager->lock_get(lt, txn);
#endif

	if (rc == RCOK) {
		row = this;
	} else if (rc == Abort) {} 
	else if (rc == WAIT) {
		ASSERT(CC_ALG == WAIT_DIE || CC_ALG == DL_DETECT);
		uint64_t starttime = get_sys_clock();
#if CC_ALG == DL_DETECT	
		bool dep_added = false;
#endif
		uint64_t endtime;
		txn->lock_abort = false;
		INC_STATS_ENC(txn->get_thd_id(), wait_cnt, 1);
		while (!txn->lock_ready && !txn->lock_abort) 
		{
#if CC_ALG == WAIT_DIE 
			continue;
#elif CC_ALG == DL_DETECT	
			uint64_t last_detect = starttime;
			uint64_t last_try = starttime;

			uint64_t now = get_sys_clock();
			if (now - starttime > g_timeout ) {
				txn->lock_abort = true;
				break;
			}
			if (g_no_dl) {
				PAUSE
				continue;
			}
			int ok = 0;
			if ((now - last_detect > g_dl_loop_detect) && (now - last_try > DL_LOOP_TRIAL)) {
				if (!dep_added) {
					ok = dl_detector.add_dep(txn->get_txn_id(), txnids, txncnt, txn->row_cnt);
					if (ok == 0)
						dep_added = true;
					else if (ok == 16)
						last_try = now;
				}
				if (dep_added) {
					ok = dl_detector.detect_cycle(txn->get_txn_id());
					if (ok == 16)  // failed to lock the deadlock detector
						last_try = now;
					else if (ok == 0) 
						last_detect = now;
					else if (ok == 1) {
						last_detect = now;
					}
				}
			} else 
				PAUSE
#endif
		}
		if (txn->lock_ready) 
			rc = RCOK;
		else if (txn->lock_abort) { 
			rc = Abort;
			return_row(type, txn, NULL);
		}
		endtime = get_sys_clock();
		INC_TMP_STATS_ENC(thd_id, time_wait, endtime - starttime);
		row = this;
	}
	return rc;
#elif CC_ALG == TIMESTAMP || CC_ALG == MVCC || CC_ALG == HEKATON 
	uint64_t thd_id = txn->get_thd_id();
	// For TIMESTAMP RD, a new copy of the row will be returned.
	// for MVCC RD, the version will be returned instead of a copy
	// So for MVCC RD-WR, the version should be explicitly copied.
	//row_t * newr = NULL;
  #if CC_ALG == TIMESTAMP
	// TODO. should not call malloc for each row read. Only need to call malloc once 
	// before simulation starts, like TicToc and Silo.
	txn->cur_row = (row_t *) malloc(sizeof(row_t));
	txn->cur_row->init(get_table(), this->get_part_id());
  #endif

	// TODO need to initialize the table/catalog information.
	TsType ts_type = (type == RD)? R_REQ : P_REQ; 
	rc = this->manager->access(txn, ts_type, row);
	if (rc == RCOK ) {
		row = txn->cur_row;
	} else if (rc == WAIT) {
		uint64_t t1 = get_sys_clock();
		while (!txn->ts_ready)
			PAUSE
		uint64_t t2 = get_sys_clock();
		INC_TMP_STATS_ENC(thd_id, time_wait, t2 - t1);
		row = txn->cur_row;
	}
	if (rc != Abort) {
		row->table = get_table();
		assert(row->get_schema() == this->get_schema());
	}
	return rc;
#elif CC_ALG == OCC
	// OCC always make a local copy regardless of read or write
	txn->cur_row = (row_t *) malloc(sizeof(row_t));
	txn->cur_row->init(get_table(), get_part_id());
	rc = this->manager->access(txn, R_REQ);
	row = txn->cur_row;
	return rc;
#elif CC_ALG == TICTOC || CC_ALG == SILO
	// like OCC, tictoc also makes a local copy for each read/write
	row->table = get_table();
	TsType ts_type = (type == RD)? R_REQ : P_REQ; 
	rc = this->manager->access(txn, ts_type, row);
	return rc;
#elif CC_ALG == HSTORE || CC_ALG == VLL
	row = this;
	return rc;
#else
	assert(false);
#endif
}

// the "row" is the row read out in get_row(). 
// For locking based CC_ALG, the "row" is the same as "this". 
// For timestamp based CC_ALG, the "row" != "this", and the "row" must be freed.
// For MVCC, the row will simply serve as a version. The version will be 
// delete during history cleanup.
// For TIMESTAMP, the row will be explicity deleted at the end of access().
// (cf. row_ts.cpp)
void row_t::return_row(access_t type, txn_man * txn, row_t * row) {	
#if CC_ALG == WAIT_DIE || CC_ALG == NO_WAIT || CC_ALG == DL_DETECT
	assert (row == NULL || row == this || type == XP);
	if (ROLL_BACK && type == XP) {// recover from previous writes.
		this->copy(row);
	}
	this->manager->lock_release(txn);
#elif CC_ALG == TIMESTAMP || CC_ALG == MVCC 
	// for RD or SCAN or XP, the row should be deleted.
	// because all WR should be companied by a RD
	// for MVCC RD, the row is not copied, so no need to free. 
  #if CC_ALG == TIMESTAMP
	if (type == RD || type == SCAN) {
		row->free_row();
		free(row);
	}
  #endif
	if (type == XP) {
		this->manager->access(txn, XP_REQ, row);
	} else if (type == WR) {
		assert (type == WR && row != NULL);
		assert (row->get_schema() == this->get_schema());
		RC rc = this->manager->access(txn, W_REQ, row);
		assert(rc == RCOK);
	}
#elif CC_ALG == OCC
	assert (row != NULL);
	if (type == WR)
		manager->write( row, txn->end_ts );
	row->free_row();
	free(row);
	return;
#elif CC_ALG == TICTOC || CC_ALG == SILO
	assert (row != NULL);
	return;
#elif CC_ALG == HSTORE || CC_ALG == VLL
	return;
#else 
	assert(false);
#endif
}

// #include "string"
// #include "vector"
// #include "coder.h"

// using namespace std;

std::string row_t::encode() {
    std::vector<std::pair<std::string, std::string> > data_items;
//    puts(table->get_table_name());
    data_items.emplace_back(std::make_pair("table_name:", std::string(table->get_table_name())));
    data_items.emplace_back(std::make_pair("primary_key:", std::to_string(_primary_key)));
    data_items.emplace_back(std::make_pair("part_id:", std::to_string(_part_id)));
    data_items.emplace_back(std::make_pair("row_id:", std::to_string(_row_id)));
    auto siz = get_tuple_size();
    std::string data_str;
    for (unsigned i=0;i<siz;i++) {
        uint8_t val = data[i];  // 256.
        data_str.push_back(val % 10 + '0');
        data_str.push_back((val/10) % 10 + '0');
        data_str.push_back((val/100) % 10 + '0');
    }
    data_items.emplace_back(make_pair("data:", data_str));
    return encode_vec(data_items);
}

std::uint64_t row_t::hash() {
    uint64_t res = 0;
    res ^= _primary_key ^ _row_id;
    auto siz = get_tuple_size();
    for (unsigned i=0;i<siz;i++) {
        auto val = (unsigned char) (data[i]);  // 256.
        res ^= uint64_t (val);
    }
    return res;
}

void row_t::decode(const std::string& e) {
    std::vector<std::pair<std::string, std::string> > data_items = decode_vec(e);
    assert(data_items.size() == 5);
    const std::string &data_s = data_items[4].second;
    this->init(int(data_s.length()));
//    auto nam = data_items[0].second.c_str();
#ifdef YCSB
//	assert(data_items[0].second == "MAIN_TABLE");
#endif
    this->table = (table_t*) tab_map->_tables[data_items[0].second];
	assert(this->table);
    memcpy(data, data_s.c_str(), data_s.length());
    for (size_t i=0;i<data_s.length();i+=3) {
        data[i] = data_s[i] - '0';
        data[i] += (data_s[i+1] - '0') * 10;
        data[i] += (data_s[i+2] - '0') * 100;
    }
    set_primary_key(int64_t(stoull(data_items[1].second)));
    _part_id = (int64_t(stoull(data_items[2].second)));
    _row_id = (int64_t(stoull(data_items[3].second)));
    init_manager(this);
//    this->manager->init(this);
}