

// original flv_splitter's comment
/* 
 *	Copyright (C) 2003-2006 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#include "../../SDK/foobar2000.h"
#include "../../helpers/helpers.h"
#include "../../shared/shared.h"
#pragma comment(lib,"../../shared/shared.lib")

#define FOO_VERSION "0.1"
DECLARE_COMPONENT_VERSION("Flv input",FOO_VERSION,
						  "Flv input"FOO_VERSION"\n\nThis is an experimental plugin, use at your own risk");
DECLARE_FILE_TYPE("Flv files","*.flv");

class stream_bitreader_ref : public stream_reader {
public:
	stream_bitreader_ref(stream_reader * p_reader) : m_reader(p_reader), m_walk(0), m_buf(0){}

	t_size read_bits(t_uint32 & p_out, t_size p_bits, abort_callback & p_abort) {
		//msb first only
		t_size done = 0;
		p_out = 0;
		while(done < p_bits){
			if(m_walk == 0){	
				if(m_reader->read(&m_buf, 1, p_abort) != 1)
					break;
			}
			t_size bits = min(8 - m_walk, p_bits - done);
			if(bits > 0){
				p_out <<= bits;
				p_out |= (m_buf >> (8 - bits));
				//remove readed bits
				m_buf <<= bits;
				done += bits;
				m_walk += bits;
				m_walk &= 0x0007;
			} else {
				break;
			}
		}
		return done;
	}

	t_size read(void * p_buffer,t_size p_bytes,abort_callback & p_abort) {
		t_size done = m_reader->read(p_buffer,p_bytes,p_abort);
		return done;
	}
	
	t_size get_bits(t_size p_bits, abort_callback & p_abort, t_size & p_result) {
		t_size ret = 0;
		p_result = read_bits(ret, p_bits, p_abort);
		return ret;
	}
	t_filesize skip(t_filesize p_bytes,abort_callback & p_abort) {
		t_filesize done = m_reader->skip(p_bytes,p_abort);
		return done;
	}
	void do_bytealign(){
		m_walk = 0;
	}
private:
	stream_reader * m_reader;
	t_size		m_walk;
	t_uint8		m_buf;
};


class flv_splitter : public input_impl
{
public:
	struct tag
	{
		UINT32 PreviousTagSize;
		BYTE TagType;
		UINT32 DataSize;
		UINT32 TimeStamp;
		UINT32 Reserved;
	};
	struct audio_info
	{
		BYTE SoundFormat;
		BYTE SoundRate;
		BYTE SoundSize;
		BYTE SoundType;
		double SoundLength;
		double bitrate;
		double audiosize;
		double lasttimestamp;

		t_size get_sample_rate() { return 44100*(1<<SoundRate)/8; }
		void clear(){
			SoundFormat = 0;
			SoundRate = 0;
			SoundSize = 0;
			SoundType = 0;
			SoundLength = 0;
			bitrate = 0;
			audiosize = 0;
			lasttimestamp = 0;
		}

	};
	bool read_tag(tag& p_out, abort_callback & p_abort)
	{
		t_size result = 0;
		stream_bitreader_ref reader(m_file.get_ptr());
		try{
			//read 4 + 1 + 3 + 3 + 4 = 15bytes
			p_out.PreviousTagSize = reader.get_bits(32, p_abort, result);
			p_out.TagType	= reader.get_bits(8, p_abort, result);
			p_out.DataSize	= reader.get_bits(24, p_abort, result);
			p_out.TimeStamp	= reader.get_bits(24, p_abort, result);
			p_out.Reserved	= reader.get_bits(32, p_abort, result);
		} catch(...){
			return false;
		}
		return result == 32;
	}

	bool read_audioinfo(audio_info& p_out, abort_callback & p_abort){
		stream_bitreader_ref reader(m_file.get_ptr());
		t_size result;
		//read 1byte
		try{
			p_out.SoundFormat = reader.get_bits(4, p_abort, result);
			p_out.SoundRate = reader.get_bits(2, p_abort, result);
			p_out.SoundSize = reader.get_bits(1, p_abort, result);
			p_out.SoundType = reader.get_bits(1, p_abort, result);
		} catch(...){
			return false;
		}
		return result == 1;
	}
	bool amf(const t_uint8 * &eb, int &es, int type){
		for(int i = 0; true; i++) {
			if(es < 2)
				return false;
			char nb[50];
			int ns = ((eb[0] << 8) | eb[1]);
			eb += 2;
			es -= 2;
			if(ns >= 50)
				return false;
			if(ns == 0) {
				if(es < 1)
					return false;
				if(*eb != 9)
					return false;
				eb += 1;
				es -= 1;
				break;
			}		
			if(es < ns)
				return false;
			memcpy(nb, eb, ns); nb[ns] = 0; eb += ns; es -= ns;
			es -= 1;
			switch(*(eb++))
			{
			case 0: // number.
				if(es < 8) return(false);
				{
					double x;
					for(int k = 0; k < 8; k++) ((char*)&x)[k] = eb[7 - k];
					if(type == -1) {
						if(pfc::strcmp_ex(nb,infinite,"audiodatarate",infinite) == 0) m_info.bitrate = x;
						else if(pfc::strcmp_ex(nb,infinite,"lasttimestamp",infinite) == 0) m_info.lasttimestamp = x;
						else if(pfc::strcmp_ex(nb,infinite,"audiosize",infinite) == 0) m_info.audiosize = x;
					}
				}
				eb += 8; es -= 8;
				break;
			case 1: // bool.
				if(es < 1) return(false);
				eb += 1; es -= 1;
				break;
			case 2: // string.
				if(es < 2) return(false);
				{
					int ss = ((*eb << 8) | eb[1]); eb += 2; es -= 2;
					if(es < ss) return(false);
					eb += ss; es -= ss;
				}
				break;
			case 3: // object.
				if(!amf(eb, es, 3)) return(false);
				break;
			case 8: // array.
				eb += 4; es -= 4;
				if(!amf(eb, es, 8)) return(false);
				break;
			case 10: // strict 4byte array.
				if(es < 4) return(false);
				{
					int as = *(eb++);
					as <<= 8; as |= *(eb++);
					as <<= 8; as |= *(eb++);
					as <<= 8; as |= *(eb++);
					es -= 4;
					eb += (9 * as); es -= (9 * as);
				}
				break;
			case 11: // date.
				eb += (8 + 2); es -= (8 + 2);
				break;
			default:
				return(false);
			}
		}
		return(true);
	}
	struct VideoTag
	{
		BYTE FrameType;
		BYTE CodecID;
	};

	void create_packet_decoder(abort_callback & p_abort, t_filesize limit, t_input_open_reason p_reason){
		stream_bitreader_ref reader(m_file.get_ptr());

		switch(m_info.SoundFormat)
		{
		case 0:
			break;
		case 2: //MP3
			{
				packet_decoder::g_open(m_decoder, p_reason == input_open_decode, packet_decoder::owner_MP3, 0 ,0, 0, p_abort);
			}
			break;
		case 10: // AAC
			{
				t_size result;
				reader.get_bits(8, p_abort, result);

				pfc::array_t<t_uint8> buf;
				t_size buf_size = limit - m_file->get_position(p_abort);
				buf.set_size(buf_size);
				reader.read(buf.get_ptr(),buf_size, p_abort);
				//I don't know how to get AAC's packet_decoder.
				if(p_reason == input_open_decode){
					throw exception_io_unsupported_format();
				}
			}
			break;
		}
		if(m_decoder.is_valid()){
			m_decoder->set_stream_property(packet_decoder::property_samplerate,44100*(1<<m_info.SoundRate)/8,0,0);
			m_decoder->set_stream_property(packet_decoder::property_bitspersample,8*(m_info.SoundSize + 1),0,0);
			m_decoder->set_stream_property(packet_decoder::property_channels,m_info.SoundType+1,0,0);
		}
	}

	bool sync(t_filesize& pos, abort_callback & p_abort) 
	{
		if(m_file->can_seek()){
			m_file->seek(pos, p_abort);
			while(m_file->get_remaining(p_abort) >= 11)
			{
				t_filesize limit = m_file->get_remaining(p_abort);

				BYTE b;
				do { m_file->read_object_t(b, p_abort); }
				while(b != 8 && b != 9 && limit-- > 0);

				pos = m_file->get_position(p_abort);

				stream_bitreader_ref reader(m_file.get_ptr());
				t_size result = 0;

				UINT32 DataSize = (UINT32)reader.get_bits(24, p_abort, result);
				UINT32 TimeStamp = (UINT32)reader.get_bits(24, p_abort, result);
				UINT32 Reserved = (UINT32)reader.get_bits(32, p_abort, result);

				t_filesize next = m_file->get_position(p_abort) + DataSize;
				
				if(next <= m_file->get_size(p_abort))
				{
					m_file->seek(next, p_abort);

					if(next == m_file->get_size(p_abort) || reader.get_bits(32, p_abort, result) == DataSize + 11)
					{
						m_file->seek(pos -= 5, p_abort);
						return true;
					}
				}

				m_file->seek(pos, p_abort);
			}
		}
		return false;
	}

	//! Opens specified file for info read / decoding / info write. This is called only once, immediately after object creation, before any other methods, and no other methods are called if open() fails.
	//! @param m_filehint Optional; passes file object to use for the operation; if set to null, the implementation should handle opening file by itself. Note that not all inputs operate on physical files that can be reached through filesystem API, some of them require this parameter to be set to null (tone and silence generators for an example). Typically, an input implementation that requires file access calls input_open_file_helper() function to ensure that file is open with relevant access mode (read or read/write).
	//! @param p_path URL of resource being opened.
	//! @param p_reason Type of operation requested. Possible values are: \n
	//! - input_open_info_read - info retrieval methods are valid; \n
	//! - input_open_decode - info retrieval and decoding methods are valid; \n
	//! - input_open_info_write - info retrieval and retagging methods are valid; \n
	//! Note that info retrieval methods are valid in all cases, and they may be called at any point of decoding/retagging process. Results of info retrieval methods (other than get_subsong_count() / get_subsong()) between retag_set_info() and retag_commit() are undefined however; those should not be called during that period.
	//! @param p_abort abort_callback object signaling user aborting the operation.
	void open(service_ptr_t<file> p_filehint,const char * p_path,t_input_open_reason p_reason,abort_callback & p_abort){
		if (p_reason == input_open_info_write)
			throw exception_io_unsupported_format();

		m_file = p_filehint;
		input_open_file_helper(m_file,p_path,p_reason,p_abort);

		stream_bitreader_ref reader(m_file.get_ptr());
		t_size result = 0;
		//read 4bytes
		if(reader.get_bits(24, p_abort, result) != 'FLV' || reader.get_bits(8, p_abort, result) != 1)
			return ;
		//read 1byte
		reader.get_bits(4, p_abort, result);
		bool fTypeFlagsAudio = !!reader.get_bits(2, p_abort, result);
		bool fTypeFlagsVideo = !!reader.get_bits(2, p_abort, result);
		//read 4byte
		m_offset = reader.get_bits(32, p_abort, result);
		reader.skip(m_offset - m_file->get_position(p_abort), p_abort);
		reader.do_bytealign();
		//find audio info
		tag t;
		m_info.clear();
		for(t_size i=0; i<500 && read_tag(t, p_abort); i++)
		{
			t_filesize next = m_file->get_position(p_abort) + t.DataSize;
			if(t.TagType == 8)
			{
				if(read_audioinfo(m_info, p_abort))
				{
					create_packet_decoder(p_abort, next, p_reason);
					break;
				}
			} else if(t.TagType == 18){
				if(t.DataSize > (1 + 2 + 10 + 1 + 4)) {
					int es = t.DataSize;
					pfc::array_t<t_uint8> buf;
					buf.set_size(es);
					reader.read(buf.get_ptr(), es, p_abort);
					const t_uint8 *eb = buf.get_ptr();
					if(memcmp(eb, "\x02\x00\x0aonMetaData\x08", (1 + 2 + 10 + 1)) == 0){
						eb += (1 + 2 + 10 + 1);
						es -= (1 + 2 + 10 + 1);
						eb += 4; es -= 4;
						//parse metadata
						amf(eb, es, -1);
						if(m_info.lasttimestamp != 0) m_info.SoundLength = m_info.lasttimestamp;
					}
				}
			}
			m_file->skip(next - m_file->get_position(p_abort), p_abort);
		}

		//search audio's last time stamp
		if(m_file->can_seek()/* && m_info.lasttimestamp == 0*/)
		{
			t_filesize pos = max(m_offset,  m_file->get_size(p_abort) >= 65536 ? m_file->get_size(p_abort) - 65536 : 0);
			if(sync(pos, p_abort))
			{
				tag t;
				audio_info at;

				while(read_tag(t, p_abort))
				{	
					t_filesize next = m_file->get_position(p_abort) + t.DataSize;
					if(m_info.SoundLength == 0 || t.TagType == 8 && read_audioinfo(at, p_abort)){
						m_info.SoundLength = max(0, t.TimeStamp * 0.001);
						if(m_info.lasttimestamp == 0)
							m_info.lasttimestamp = m_info.SoundLength;
					}
					m_file->seek(next, p_abort);
				}
			}
			m_file->seek(m_offset, p_abort);
		}
	}

	//! See: input_info_reader::get_subsong_count(). Valid after open() with any reason.
	unsigned get_subsong_count(){ return 1;}
	//! See: input_info_reader::get_subsong(). Valid after open() with any reason.
	t_uint32 get_subsong(unsigned p_index){	return 0; }
	//! See: input_info_reader::get_info(). Valid after open() with any reason.
	void get_info(t_uint32 p_subsong,file_info & p_info,abort_callback & p_abort){
		p_info.set_length(m_info.SoundLength);
		p_info.info_set_bitrate(m_info.bitrate+0.5);

		p_info.info_set_int("samplerate",m_info.get_sample_rate());
		p_info.info_set_int("channels", m_info.SoundType+1);
		p_info.info_set_int("bitspersample",8*(m_info.SoundSize + 1));
		if(m_info.SoundFormat == 2){
			p_info.info_set("codec", "MP3");
			p_info.info_set("encoding", "lossy");
		} else if(m_info.SoundFormat == 10){
			p_info.info_set("codec", "AAC");
			p_info.info_set("encoding", "lossy");
		}
		/*
		if(m_decoder.is_valid())
			m_decoder->get_info(p_info);
			*/
	}
	//! See: input_info_reader::get_file_stats(). Valid after open() with any reason.
	t_filestats get_file_stats(abort_callback & p_abort){
		return m_file->get_stats(p_abort);
	}

	//! See: input_decoder::initialize(). Valid after open() with input_open_decode reason.
	void decode_initialize(t_uint32 p_subsong,unsigned p_flags,abort_callback & p_abort){
		m_packet_counter = 0;
		m_sample_counter = 0;
		m_packet_timestamp = 0;
		m_byte_counter = 0;

		m_lasttime_for_vbr = 0;
		m_byte_counter_for_vbr = 0;

		if(m_file->can_seek()){
			m_file->seek(m_offset, p_abort);
		}
		else if(m_offset > m_file->get_position(p_abort)){
			m_file->skip(m_offset - m_file->get_position(p_abort), p_abort);
		}
	}
	//! See: input_decoder::run(). Valid after decode_initialize().
	bool decode_run(audio_chunk & p_chunk,abort_callback & p_abort){
		tag t;
		audio_info at;
		t_size result;
		stream_bitreader_ref reader(m_file.get_ptr());

		while(read_tag(t, p_abort))
		{
			t_filesize next = m_file->get_position(p_abort) + t.DataSize;
			if(t.DataSize != 0 && t.TagType == 8 && read_audioinfo(at, p_abort))
			{
				if(at.SoundFormat != 10 || reader.get_bits(8, p_abort, result)==1)
				{
					t_size buf_size = next - m_file->get_position(p_abort);			
					pfc::array_t<t_uint8> buf;
					buf.set_size(buf_size);
					t_size rb = m_file->read(buf.get_ptr(), buf_size, p_abort);
					if(rb == 0){
						// eof
						return false;
					}
					{
						m_byte_counter += buf_size;
						m_packet_timestamp = t.TimeStamp * 0.001;
						m_packet_counter += 1;

						if(m_decoder.is_valid()){
							m_decoder->decode(buf.get_ptr(), buf_size, p_chunk, p_abort);
							if(p_chunk.get_sample_count() != 0){								
								m_sample_counter += p_chunk.get_sample_count();
								m_byte_counter_for_vbr += buf_size;
								return true;
							}
						} else {
							return false;
						}
					}
				}
			}
			m_file->skip(next - m_file->get_position(p_abort), p_abort);
		}
		return false;
	}
	//! See: input_decoder::seek(). Valid after decode_initialize().
	void decode_seek(double p_seconds,abort_callback & p_abort){
		m_file->ensure_seekable();
		t_filesize pos = m_offset + (m_info.SoundLength != 0 ? p_seconds/m_info.SoundLength * (m_file->get_size(p_abort) - m_offset) : (m_file->get_size(p_abort) - m_offset)/2);
		if(!sync(pos, p_abort))
		{
			m_file->seek(m_offset, p_abort);
			return;
		}

		tag t;

		while(read_tag(t, p_abort))
		{
			if(t.TagType == 8 && t.TimeStamp * 0.001 >= p_seconds)
			{
				m_file->seek(m_file->get_position(p_abort) - 15, p_abort);
				break;
			}
			if(m_file->get_position(p_abort) + t.DataSize < m_file->get_size(p_abort))
				m_file->seek(m_file->get_position(p_abort) + t.DataSize, p_abort);
			else
				break;
		}

		while(m_file->get_position(p_abort) >= m_offset && read_tag(t, p_abort))
		{
			t_filesize prev = m_file->get_position(p_abort) - 15 - t.PreviousTagSize - 4;

			if(t.TagType == 8 && t.TimeStamp*0.001 <= p_seconds)
			{
				m_file->seek(m_file->get_position(p_abort) - 15, p_abort);
				return ;
			}
			if(prev >= m_offset && t.PreviousTagSize > 4)
				m_file->seek(prev, p_abort);
			else{				
				m_file->seek(m_file->get_position(p_abort) - 15, p_abort);
				return;
			}
		}
		m_lasttime_for_vbr = m_packet_timestamp = t.TimeStamp + 0.001;		
		m_byte_counter_for_vbr = 0; 
	}
	//! See: input_decoder::can_seek(). Valid after decode_initialize().
	bool decode_can_seek(){
		return m_file->can_seek();
	}
	//! See: input_decoder::get_dynamic_info(). Valid after decode_initialize().
	bool decode_get_dynamic_info(file_info & p_out, double & p_timestamp_delta){
		if(m_packet_timestamp >= m_lasttime_for_vbr + 0.5)
		{
			double bitrate = 8 * m_byte_counter_for_vbr / (m_packet_timestamp - m_lasttime_for_vbr)/1000;
			p_timestamp_delta = m_packet_timestamp - m_lasttime_for_vbr;
			p_out.info_set_bitrate_vbr(bitrate + 0.5);
			m_lasttime_for_vbr = m_packet_timestamp;
			m_byte_counter_for_vbr = 0;
#if 0
			console::print(pfc::string8()<< "packet counter:" << m_packet_counter << "  bytes counter:" << m_byte_counter);
			console::print(pfc::string8()<< "timestamp:" <<m_packet_timestamp << " pcm time:" << m_sample_counter /(double) m_info.get_sample_rate());
#endif
			return true;
		}
		return false;
	}
	//! See: input_decoder::get_dynamic_info_track(). Valid after decode_initialize().
	bool decode_get_dynamic_info_track(file_info & p_out, double & p_timestamp_delta){
		return false;
	}
	//! See: input_decoder::on_idle(). Valid after decode_initialize().
	void decode_on_idle(abort_callback & p_abort){ 
		m_file->on_idle(p_abort);
	}

	//! See: input_info_writer::set_info(). Valid after open() with input_open_info_write reason.
	void retag_set_info(t_uint32 p_subsong,const file_info & p_info,abort_callback & p_abort){
		throw exception_io_unsupported_format();
	}
	//! See: input_info_writer::commit(). Valid after open() with input_open_info_write reason.
	void retag_commit(abort_callback & p_abort){
		throw exception_io_unsupported_format();
	}
	
	//! See: input_entry::is_our_content_type().
	static bool g_is_our_content_type(const char * p_content_type){
		return !pfc::strcmp_ex(p_content_type, infinite, "flv/mp3", infinite);
	}

	//! See: input_entry::is_our_path().
	static bool g_is_our_path(const char * p_path,const char * p_extension){
		return !pfc::strcmp_ex(p_extension, infinite, "flv", infinite);
	}

	flv_splitter() {}
	~flv_splitter() {}	
protected:
	service_ptr_t<packet_decoder> m_decoder;
	service_ptr_t<file> m_file;
	t_size m_offset;
	audio_info m_info;

	t_size m_packet_counter;
	t_filesize m_sample_counter;

	double m_packet_timestamp;
	t_size m_byte_counter;

	double m_lasttime_for_vbr;
	t_size m_byte_counter_for_vbr;
};

input_factory_t<flv_splitter> g_flv;