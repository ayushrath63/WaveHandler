#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <variant>
#include <vector>
#include <array>
#include <bitset>

namespace Wave
{

template<typename T = float>
class File
{
public:
	
	File<T>(){}

	File<T>(std::string filepath)
	{
		load(filepath);
	}

	~File() {}

	bool load(std::string filepath);
	bool write();
	bool write(std::string filepath);

	uint16_t getAudioFormat() const
	{
		return d_->audio_format;
	}

	uint16_t getNumChannels() const
	{
		return d_->num_channels;
	}

	uint32_t getSampleRate() const
	{
		return d_->sample_rate;
	}

	uint16_t getBitDepth() const
	{
		return d_->bits_per_sample;
	}

	uint32_t getNumSamples() const
	{
		return (8 * d_->data_chunk_size) / (d_->num_channels * d_->bits_per_sample);
	}

	float getDurationSeconds() const
	{
		return static_cast<float>(getNumSamples()) / d_->sample_rate;
	}

private:
	T clamp_range(T val)
	{
		if (val < static_cast<T>(-1.0f)) return static_cast<T>(-1.0f);
		else if (val > static_cast<T>(1.0f)) return static_cast<T>(1.0f);
		else return val;
	}
	struct Data
	{
		uint8_t chunk_id[4];
		uint32_t chunk_size;
		uint8_t format[4];

		uint8_t fmt_chunk_id[4];
		uint32_t fmt_chunk_size;
		uint16_t audio_format;
		uint16_t num_channels;
		uint32_t sample_rate;
		uint32_t byte_rate;
		uint16_t block_align;
		uint16_t bits_per_sample;

		uint8_t data_chunk_id[4];
		uint32_t data_chunk_size;

		std::vector<std::vector<T>> samples;
	};

	std::string filepath_;
	std::unique_ptr<Data> d_;
};

template<typename T>
bool File<T>::load(std::string filepath)
{
	filepath_ = filepath;
	if (!d_)
	{
		d_ = std::make_unique<Data>();
	}

	FILE* fp = fopen(filepath.c_str(), "r+b");
	if (!fp)
	{
		std::cerr << "File not found\n";
		return false;
	}

	//Read chunk descriptor
	fread(&(d_->chunk_id[0]), sizeof(uint8_t), 4, fp);
	fread(&(d_->chunk_size), sizeof(uint32_t), 1, fp);
	fread(&(d_->format[0]), sizeof(uint8_t), 4, fp);

	std::array<uint8_t, 4> chunk_id_buf;
	uint32_t chunk_size_buf;
	//while last chunk was not the data chunk
	while (
		(chunk_id_buf[0] ^ 'd') ||
		(chunk_id_buf[1] ^ 'a') ||
		(chunk_id_buf[2] ^ 't') ||
		(chunk_id_buf[3] ^ 'a')
		)
	{
		fread(&(chunk_id_buf[0]), sizeof(uint8_t), 4, fp);
		fread(&(chunk_size_buf), sizeof(uint32_t), 1, fp);
		//Read format sub-chunk data
		if (
			!(chunk_id_buf[0] ^ 'f') &&
			!(chunk_id_buf[1] ^ 'm') &&
			!(chunk_id_buf[2] ^ 't') &&
			!(chunk_id_buf[3] ^ ' ')
			)
		{
			std::copy(chunk_id_buf.begin(), chunk_id_buf.end(), d_->fmt_chunk_id);
			d_->fmt_chunk_size = chunk_size_buf;
			fread(&(d_->audio_format), sizeof(uint16_t), 1, fp);
			fread(&(d_->num_channels), sizeof(uint16_t), 1, fp);
			fread(&(d_->sample_rate), sizeof(uint32_t), 1, fp);
			fread(&(d_->byte_rate), sizeof(uint32_t), 1, fp);
			fread(&(d_->block_align), sizeof(uint16_t), 1, fp);
			fread(&(d_->bits_per_sample), sizeof(uint16_t), 1, fp);
			d_->samples.resize(d_->num_channels);
		}
		//Read data sub-chunk data
		else if (
			!(chunk_id_buf[0] ^ 'd') &&
			!(chunk_id_buf[1] ^ 'a') &&
			!(chunk_id_buf[2] ^ 't') &&
			!(chunk_id_buf[3] ^ 'a')
			)
		{
			std::copy(chunk_id_buf.begin(), chunk_id_buf.end(), d_->data_chunk_id);
			d_->data_chunk_size = chunk_size_buf;

			uint32_t num_samples = getNumSamples();
			/*std::cout << "Number of Samples " << num_samples << std::endl;*/

			uint32_t size_of_each_sample = (d_->num_channels * d_->bits_per_sample) / 8;
			//std::cout << "Size of each sample: " << size_of_each_sample << " bytes" << std::endl;

			float duration_in_seconds = getDurationSeconds();
			//std::cout << "Approx.Duration in seconds =" << duration_in_seconds << std::endl;

			if(
				d_->bits_per_sample != 8 &&
				d_->bits_per_sample != 16 && 
				d_->bits_per_sample != 24 && 
				d_->bits_per_sample != 32
			)
			{
				std::cerr << d_->bits_per_sample << " bits per sample is not supported" << std::endl;
				return false;
			}

			std::array<uint8_t, 4> buf = { 0,0,0,0 };

			for (uint32_t curr_sample_idx = 0; curr_sample_idx < num_samples; curr_sample_idx++)
			{
				for (uint32_t channel = 0; channel < d_->num_channels; ++channel)
				{
					switch (d_->bits_per_sample)
					{
					case 8:
					{
						fread(&(buf[0]), sizeof(uint8_t), 1, fp);
						d_->samples[channel].push_back(static_cast<T>(buf[0] - 0x80) / static_cast<T>(0x80));
					}
					break;

					case 16:
					{
						fread(&(buf[0]), sizeof(uint8_t), 2, fp);
						int16_t tmp = ((uint16_t)buf[1] << 8 | (uint16_t)buf[0]);
						d_->samples[channel].push_back(static_cast<T>(tmp) / static_cast<T>(0x8000));
					}
					break;

					case 24:
					{
						fread(&(buf[0]), sizeof(uint8_t), 3, fp);
						int32_t tmp =
							(uint32_t)buf[2] << 16 |
							(uint32_t)buf[1] << 8 |
							(uint32_t)buf[0];
						//sign extend if 24th bit is set
						tmp = (tmp & 0x800000) ? tmp | ~0xFFFFFF : tmp;
						d_->samples[channel].push_back(static_cast<T>(tmp) / static_cast<T>(0x800000));
					}
					break;

					case 32:
					{
						fread(&(buf[0]), sizeof(uint8_t), 4, fp);
						int32_t tmp =
							(uint32_t)buf[3] << 24 |
							(uint32_t)buf[2] << 16 |
							(uint32_t)buf[1] << 8 |
							(uint32_t)buf[0];
						d_->samples[channel].push_back(static_cast<T>(tmp) / static_cast<T>(0x80000000));
					}
					break;

					default:
					{
						return false;
						std::cerr << d_->bits_per_sample << " bits per sample is not supported" << std::endl;
					}
					break;
					}
				}
			}
		}
		//skip over unnecessary data subchunks
		else
		{
			fseek(fp, chunk_size_buf, SEEK_CUR);
		}
	}

	fclose(fp);

	return true;
}

template<typename T>
bool File<T>::write()
{
	return write(filepath_);
}

template<typename T>
bool File<T>::write(std::string filepath)
{
	FILE* fp = fopen(filepath.c_str(), "w+b");
	if (!fp)
	{
		std::cerr << "File not found\n";
		return false;
	}

	//Read chunk descriptor
	fwrite(&(d_->chunk_id[0]), sizeof(uint8_t), 4, fp);
	fwrite(&(d_->chunk_size), sizeof(uint32_t), 1, fp);
	fwrite(&(d_->format[0]), sizeof(uint8_t), 4, fp);

	//Read format sub-chunk data
	fwrite(&(d_->fmt_chunk_id[0]), sizeof(uint8_t), 4, fp);
	fwrite(&(d_->fmt_chunk_size), sizeof(uint32_t), 1, fp);
	fwrite(&(d_->audio_format), sizeof(uint16_t), 1, fp);
	fwrite(&(d_->num_channels), sizeof(uint16_t), 1, fp);
	fwrite(&(d_->sample_rate), sizeof(uint32_t), 1, fp);
	fwrite(&(d_->byte_rate), sizeof(uint32_t), 1, fp);
	fwrite(&(d_->block_align), sizeof(uint16_t), 1, fp);
	fwrite(&(d_->bits_per_sample), sizeof(uint16_t), 1, fp);
	//Read data sub-chunk data
	fwrite(&(d_->data_chunk_id[0]), sizeof(uint8_t), 4, fp);
	fwrite(&(d_->data_chunk_size), sizeof(uint32_t), 1, fp);

	//TODO: ADD CHECKS FOR FILE INTEGRITY

	for (uint32_t curr_sample_idx = 0; curr_sample_idx < getNumSamples(); curr_sample_idx++)
	{
		for (uint32_t channel = 0; channel < getNumChannels(); ++channel)
		{
			T tmp = clamp_range(d_->samples[channel][curr_sample_idx]);
			switch (d_->bits_per_sample)
			{
			case 8:
			{
				tmp = (tmp + static_cast<T>(1.0f)) / static_cast<T>(2.0f) * static_cast<T>(0xFF);
				fwrite(&(static_cast<uint8_t>(tmp)), sizeof(uint8_t), 1, fp);
			}
			break;

			case 16:
			{
				tmp *= static_cast<T>(0x7FFF);
				std::array<uint8_t, 2> buf = {
					static_cast<int16_t>(tmp) & 0xFF,
					(static_cast<int16_t>(tmp) >> 8) & 0xFF,
				};
				fwrite(&(buf[0]), sizeof(uint8_t), 2, fp);
			}
			break;

			case 24:
			{
				tmp *= static_cast<T>(0x7FFFFF);
				std::array<uint8_t, 3> buf = {
					static_cast<int32_t>(tmp) & 0xFF,
					(static_cast<int32_t>(tmp) >> 8) & 0xFF,
					(static_cast<int32_t>(tmp) >> 16) & 0xFF,
				};
				fwrite(&(buf[0]), sizeof(uint8_t), 3, fp);
			}
			break;

			case 32:
			{
				tmp *= static_cast<T>(0x7FFFFFFF);
				std::array<uint8_t, 4> buf = {
					static_cast<int32_t>(tmp) & 0xFF,
					(static_cast<int32_t>(tmp) >> 8) & 0xFF,
					(static_cast<int32_t>(tmp) >> 16) & 0xFF,
					(static_cast<int32_t>(tmp) >> 24) & 0xFF,
				};
				fwrite(&(buf[0]), sizeof(uint8_t), 4, fp);

			}
			break;

			default:
			{
				return false;
				std::cerr << d_->bits_per_sample << " bits per sample is not supported" << std::endl;
			}
			break;
			}
		}
	}
	fclose(fp);

	return true;
}


}
