#pragma once

#include <DB/Storages/MarkCache.h>
#include <DB/Storages/MergeTree/MarkRange.h>
#include <DB/Storages/MergeTree/MergeTreeData.h>
#include <DB/Columns/ColumnNullable.h>
#include <DB/Core/NamesAndTypes.h>
#include <DB/IO/CachedCompressedReadBuffer.h>
#include <DB/IO/CompressedReadBufferFromFile.h>


namespace DB
{


namespace ErrorCodes
{
	extern const int NOT_FOUND_EXPECTED_DATA_PART;
	extern const int MEMORY_LIMIT_EXCEEDED;
}

class IDataType;

/** Умеет читать данные между парой засечек из одного куска. При чтении последовательных отрезков не делает лишних seek-ов.
  * При чтении почти последовательных отрезков делает seek-и быстро, не выбрасывая содержимое буфера.
  */
class MergeTreeReader
{
public:
	using ValueSizeMap = std::map<std::string, double>;

	MergeTreeReader(const String & path, /// Путь к куску
		const MergeTreeData::DataPartPtr & data_part, const NamesAndTypesList & columns,
		UncompressedCache * uncompressed_cache,
		MarkCache * mark_cache,
		bool save_marks_in_cache,
		MergeTreeData & storage, const MarkRanges & all_mark_ranges,
		size_t aio_threshold, size_t max_read_buffer_size,
		const ValueSizeMap & avg_value_size_hints = ValueSizeMap{},
		const ReadBufferFromFileBase::ProfileCallback & profile_callback = ReadBufferFromFileBase::ProfileCallback{},
		clockid_t clock_type = CLOCK_MONOTONIC_COARSE);

	MergeTreeReader(const MergeTreeReader &) = delete;
	MergeTreeReader & operator=(const MergeTreeReader &) = delete;

	const ValueSizeMap & getAvgValueSizeHints() const;

	/** Если столбцов нет в блоке, добавляет их, если есть - добавляет прочитанные значения к ним в конец.
	  * Не добавляет столбцы, для которых нет файлов. Чтобы их добавить, нужно вызвать fillMissingColumns.
	  * В блоке должно быть либо ни одного столбца из columns, либо все, для которых есть файлы.
	  */
	void readRange(size_t from_mark, size_t to_mark, Block & res);

	/** Добавляет в блок недостающие столбцы из ordered_names, состоящие из значений по-умолчанию.
	  * Недостающие столбцы добавляются в позиции, такие же как в ordered_names.
	  * Если был добавлен хотя бы один столбец - то все столбцы в блоке переупорядочиваются как в ordered_names.
	  */
	void fillMissingColumns(Block & res, const Names & ordered_names, const bool always_reorder = false);

	/** То же самое, но всегда переупорядочивает столбцы в блоке, как в ordered_names
	  *  (даже если не было недостающих столбцов).
	  */
	void fillMissingColumnsAndReorder(Block & res, const Names & ordered_names);

private:
	void addStream(const String & name, const IDataType & type, const MarkRanges & all_mark_ranges,
		const ReadBufferFromFileBase::ProfileCallback & profile_callback, clockid_t clock_type,
		size_t level = 0);

	void readData(const String & name, const IDataType & type, IColumn & column, size_t from_mark, size_t max_rows_to_read,
		size_t level = 0, bool read_offsets = true);

	void fillMissingColumnsImpl(Block & res, const Names & ordered_names, bool always_reorder);

private:
	class Stream
	{
	public:
		Stream(
			const String & path_prefix_, const String & extension_,
			UncompressedCache * uncompressed_cache,
			MarkCache * mark_cache, bool save_marks_in_cache,
			const MarkRanges & all_mark_ranges, size_t aio_threshold, size_t max_read_buffer_size,
			const ReadBufferFromFileBase::ProfileCallback & profile_callback, clockid_t clock_type);

		Stream(Stream &&) = default;
		Stream & operator=(Stream &&) = default;

		void loadMarks(MarkCache * cache, bool save_in_cache, bool is_null_stream);

		void seekToMark(size_t index);

	public:
		ReadBuffer * data_buffer;

	private:
		MarkCache::MappedPtr marks;
		std::unique_ptr<CachedCompressedReadBuffer> cached_buffer;
		std::unique_ptr<CompressedReadBufferFromFile> non_cached_buffer;
		std::string path_prefix;
		std::string extension;
		size_t max_mark_range;
	};

	using FileStreams = std::map<std::string, std::unique_ptr<Stream>>;

private:
	/// Используется в качестве подсказки, чтобы уменьшить количество реаллокаций при создании столбца переменной длины.
	ValueSizeMap avg_value_size_hints;
	String path;
	MergeTreeData::DataPartPtr data_part;
	FileStreams streams;

	/// Запрашиваемые столбцы.
	NamesAndTypesList columns;

	UncompressedCache * uncompressed_cache;
	MarkCache * mark_cache;
	/// Если выставлено в false - при отсутствии засечек в кэше, считавать засечки, но не сохранять их в кэш, чтобы не вымывать оттуда другие данные.
	bool save_marks_in_cache;

	MergeTreeData & storage;
	MarkRanges all_mark_ranges;
	size_t aio_threshold;
	size_t max_read_buffer_size;
};

}
