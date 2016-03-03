#ifndef MULTIVERSO_MATRIX_TABLE_H_
#define MULTIVERSO_MATRIX_TABLE_H_

#include "multiverso/table_interface.h"
#include "multiverso/util/log.h"
#include "multiverso/zoo.h"

#include <vector>

namespace multiverso {

	template <typename T>
	class MatrixWorkerTable : public WorkerTable {
	public:
		explicit MatrixWorkerTable(int num_row, int num_col) : WorkerTable(), num_row_(num_row), num_col_(num_col) {
			num_server_ = Zoo::Get()->num_servers();
			server_offsets_.push_back(0);
			int length = num_row / num_server_;
			for (int i = 1; i < num_server_; ++i) {
				server_offsets_.push_back(i * length); // may not balance
			}
			server_offsets_.push_back(num_row);

			Log::Debug("worker %d create matrixTable with %d rows %d colums.\n", Zoo::Get()->rank(), num_row, num_col);
		}

		T* raw() { return data_; }

		// data is user-allocated memory
		void Get(int row_id, T* data){
			data_ = data;
			WorkerTable::Get(Blob(&row_id, sizeof(int)));
			Log::Debug("worker %d getting row with id %d.\n", Zoo::Get()->rank(), row_id);
		}

		void Add(int row_id, T* data) {
			if (row_id == -1){
				WorkerTable::Add(Blob(&row_id, sizeof(int)), Blob(data, sizeof(T) * num_col_ * num_row_));
			}
			else{
				WorkerTable::Add(Blob(&row_id, sizeof(int)), Blob(data, sizeof(T)* num_col_));
			}
			Log::Debug("worker %d adding row with id %d.\n", Zoo::Get()->rank(), row_id);
		}

		// data is user-allocated memory
		void Get(std::vector<int> row_ids, T* data) {
			data_ = data;
			WorkerTable::Get(Blob(&row_ids[0], sizeof(int)* row_ids.size()));
			Log::Debug("worker %d getting rows\n", Zoo::Get()->rank());
		}

		// Add some rows
		void Add(std::vector<int> row_ids, T* data) {
			Blob ids_blob(&row_ids[0], sizeof(int)* row_ids.size());
			Blob data_blob(data, sizeof(T)* row_ids.size() * num_col_);
			WorkerTable::Add(ids_blob, data_blob);
			Log::Debug("worker %d adding rows\n", Zoo::Get()->rank());
		}

		int Partition(const std::vector<Blob>& kv,
			std::unordered_map<int, std::vector<Blob> >* out) override {
			CHECK(kv.size() == 1 || kv.size() == 2);
			CHECK_NOTNULL(out);

			//get all elements
			if (kv[0].size<int>() == 1 && kv[0].As<int>(0) == -1){
				for (int i = 0; i < num_server_; ++i){
					(*out)[i].push_back(kv[0]);
				}
				if (kv.size() == 2){
					for (int i = 0; i < num_server_; ++i){
						Blob blob(kv[1].data() + server_offsets_[i] * num_col_ * sizeof(T),
							(server_offsets_[i + 1] - server_offsets_[i]) * num_col_ * sizeof(T));
						(*out)[i].push_back(blob);
					}
				}
				return static_cast<int>(out->size());
			}

			Blob row_ids = kv[0];
			std::unordered_map<int, int> count;
			for (int i = 0; i < row_ids.size<int>(); ++i){
				int dst = row_ids.As<int>(i) / (num_row_ / num_server_);
				dst = (dst == num_server_ ? dst - 1 : dst);
				++count[dst];
			}
			for (auto& it : count) { // Allocate memory
				std::vector<Blob>& vec = (*out)[it.first];
				vec.push_back(Blob(it.second * sizeof(int)));
				if (kv.size() == 2) vec.push_back(Blob(it.second * sizeof(T)* num_col_));
			}
			count.clear();
			for (int i = 0; i < row_ids.size<int>(); ++i) {
				int dst = row_ids.As<int>(i) / (num_row_ / num_server_);
				dst = (dst == num_server_ ? dst - 1 : dst);
				(*out)[dst][0].As<int>(count[dst]) = row_ids.As<int>(i);
				if (kv.size() == 2){
					memcpy(&((*out)[dst][1].As<T>(count[dst] * num_col_)), &(kv[1].As<T>(i * num_col_)), num_col_ * sizeof(T));
				}
				++count[dst];
			}
			return static_cast<int>(out->size());
		}

		void ProcessReplyGet(std::vector<Blob>& reply_data) override {
			CHECK(reply_data.size() == 2 || reply_data.size() == 3);
			Blob keys = reply_data[0], data = reply_data[1];

			if (keys.size<int>() == 1 && keys.As<int>(0) == -1) {
				int row_offset = reply_data[2].As<int>();
				memcpy(data_ + row_offset * num_col_, data.data(), data.size());
				return;
			}

			CHECK(data.size() == keys.size<int>() * sizeof(T)* num_col_);
			for (int i = 0; i < keys.size<int>(); ++i) {
				int row_id = keys.As<int>(i);
				memcpy(data_ + row_id * num_col_, data.data() + i * num_col_ * sizeof(T), num_col_ * sizeof(T));
			}
		}

	private:
		T* data_; // not owned
		int num_row_;
		int num_col_;
		int num_server_;
		std::vector<size_t> server_offsets_;
	};

	// TODO(feiga): rename. The name static is inherited from last version
	// The storage is a continuous large chunk of memory
	template <typename T>
	class MatrixServerTable : public ServerTable {
	public:
		explicit MatrixServerTable(int num_row, int num_col) : ServerTable(), num_col_(num_col) {
			server_id_ = Zoo::Get()->rank();

			int size = num_row / Zoo::Get()->num_servers();
			row_offset_ = size * Zoo::Get()->rank();
			if (server_id_ == Zoo::Get()->num_servers() - 1){
				size = num_row - row_offset_;
			}
			storage_.resize(size * num_col);

			Log::Debug("server %d create matrixTable with %d row %d colums of %d rows.\n", server_id_, num_row, num_col, size);
		}

		void ProcessAdd(const std::vector<Blob>& data) override {
#ifdef MULTIVERSO_USE_BLAS
			// MKL update
			Log::Fatal("Not implemented yet\n");
#else
			CHECK(data.size() == 2);
			Blob values = data[1], keys = data[0];

			if (keys.size<int>() == 1 && keys.As<int>() == -1){
				CHECK(storage_.size() == values.size<T>());
				for (int i = 0; i < storage_.size(); ++i){
					storage_[i] += values.As<T>(i);
				}
				Log::Debug("server %d adding all rows with row offset %d with %d rows\n", server_id_, row_offset_, storage_.size() / num_col_);
				return;
			}

			CHECK(values.size() == keys.size<int>() * sizeof(T)* num_col_);
			for (int i = 0; i < keys.size<int>(); ++i) {
				int offset_v = i * num_col_;
				int offset_s = (keys.As<int>(i) -row_offset_) * num_col_;
				for (int j = 0; j < num_col_; ++j){
					storage_[j + offset_s] += values.As<T>(offset_v + j);
				}
				Log::Debug("server %d adding row with id %d\n", server_id_, keys.As<int>(i));
			}
#endif
		}

		void ProcessGet(const std::vector<Blob>& data,
			std::vector<Blob>* result) override {
			CHECK(data.size() == 1);
			CHECK_NOTNULL(result);
			
			Blob keys = data[0];
			result->push_back(keys); // also push the key

			//get all rows
			if (keys.size<int>() == 1 && keys.As<int>() == -1){
				result->push_back(Blob(storage_.data(), sizeof(T)* storage_.size()));
				result->push_back(Blob(&row_offset_, sizeof(int)));
				Log::Debug("server %d getting all rows with row offset %d with %d rows\n", server_id_, row_offset_, storage_.size() / num_col_);
				return;
			}


			result->push_back(Blob(keys.size<int>() * sizeof(T)* num_col_));
			Blob& vals = (*result)[1];
			for (int i = 0; i < keys.size<int>(); ++i) {
				int offset_v = i * num_col_;
				int offset_s = (keys.As<int>(i) -row_offset_) * num_col_;
				memcpy(&(vals.As<T>(offset_v)), &storage_[offset_s], sizeof(T)*num_col_);
				Log::Debug("server %d getting row with id %d\n", server_id_, keys.As<int>(i));
			}
		}

	private:
		int server_id_;
		int num_col_;
		int row_offset_;
		std::vector<T> storage_;
	};
}

#endif // MULTIVERSO_ARRAY_TABLE_H_
