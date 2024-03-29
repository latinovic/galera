/*
 * Copyright (C) 2010-2012 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_DATAGRAM_HPP
#define GCOMM_DATAGRAM_HPP

#include "gu_buffer.hpp"
#include "gu_serialize.hpp"
#include "gu_utils.hpp"
#include <boost/crc.hpp>

#include <limits>

#include <cstring>
#include <stdint.h>

namespace gcomm
{

    //!
    // @class NetHeader
    //
    // @brief Header for datagrams sent over network
    //
    // Header structure is the following (MSB first)
    //
    // | version(4) | reserved(3) | F_CRC(1) | len(24) |
    // |                   CRC(32)                     |
    //
    class NetHeader
    {
    public:

        NetHeader()
            :
            len_(),
            crc32_()
        { }

        NetHeader(uint32_t len, int version)
            :
            len_(len),
            crc32_(0)
        {
            if (len > len_mask_)
                gu_throw_error(EINVAL) << "msg too long " << len_;
            len_ |= (static_cast<uint32_t>(version) << version_shift_);
        }

        uint32_t len() const { return (len_ & len_mask_); }

        void set_crc32(uint32_t crc32)
        {
            crc32_ = crc32;
            len_ |= F_CRC32;
        }

        bool has_crc32() const { return (len_ & F_CRC32); }
        uint32_t crc32() const { return crc32_; }
        int version() const { return ((len_ & version_mask_) >> version_shift_); }
        friend size_t serialize(const NetHeader& hdr, gu::byte_t* buf, size_t buflen,
                                size_t offset);
        friend size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset,
                                  NetHeader& hdr);
        friend size_t serial_size(const NetHeader& hdr);
        static const size_t serial_size_ = 8;

    private:
        static const uint32_t len_mask_      = 0x00ffffff;
        static const uint32_t flags_mask_    = 0x0f000000;
        static const uint32_t flags_shift_   = 24;
        static const uint32_t version_mask_  = 0xf0000000;
        static const uint32_t version_shift_ = 28;
        enum
        {
            F_CRC32 = 1 << 24
        };
        uint32_t len_;
        uint32_t crc32_;
    };

    inline size_t serialize(const NetHeader& hdr, gu::byte_t* buf,
                            size_t buflen, size_t offset)
    {
        offset = gu::serialize4(hdr.len_, buf, buflen, offset);
        offset = gu::serialize4(hdr.crc32_, buf, buflen, offset);
        return offset;
    }

    inline size_t unserialize(const gu::byte_t* buf, size_t buflen,
                              size_t offset, NetHeader& hdr)
    {
        offset = gu::unserialize4(buf, buflen, offset, hdr.len_);
        offset = gu::unserialize4(buf, buflen, offset, hdr.crc32_);
        switch (hdr.version())
        {
        case 0:
            if ((hdr.len_ & NetHeader::flags_mask_) & ~NetHeader::F_CRC32)
            {
                gu_throw_error(EPROTO)
                    << "invalid flags "
                    << ((hdr.len_ & NetHeader::flags_mask_) >>
                        NetHeader::flags_shift_);
            }
            break;
        default:
            gu_throw_error(EPROTO) << "invalid protocol version "
                                   << hdr.version();
            throw; // keep compiler happy
        }

        return offset;
    }

    inline size_t serial_size(const NetHeader& hdr)
    {
        return NetHeader::serial_size_;
    }

    /*!
     * @brief  Datagram container
     *
     * Datagram class provides consistent interface for managing
     * datagrams/byte buffers.
     */
    class Datagram
    {
    public:
        Datagram()
            :
            header_       (),
            header_offset_(header_size_),
            payload_      (new gu::Buffer()),
            offset_       (0)
        { }
        /*!
         * @brief Construct new datagram from byte buffer
         *
         * @param[in] buf Const pointer to data buffer
         * @param[in] buflen Length of data buffer
         *
         * @throws std::bad_alloc
         */

        Datagram(const gu::Buffer& buf, size_t offset = 0)
            :
            header_       (),
            header_offset_(header_size_),
            payload_      (new gu::Buffer(buf)),
            offset_       (offset)
        {
            assert(offset_ <= payload_->size());
        }

        Datagram(const gu::SharedBuffer& buf, size_t offset = 0)
            :
            header_       (),
            header_offset_(header_size_),
            payload_      (buf),
            offset_       (offset)
        {
            assert(offset_ <= payload_->size());
        }

        /*!
         * @brief Copy constructor.
         *
         * @note Only for normalized datagrams.
         *
         * @param[in] dgram Datagram to make copy from
         * @param[in] off
         * @throws std::bad_alloc
         */
        Datagram(const Datagram& dgram,
                 size_t off = std::numeric_limits<size_t>::max()) :
            // header_(dgram.header_),
            header_offset_(dgram.header_offset_),
            payload_(dgram.payload_),
            offset_(off == std::numeric_limits<size_t>::max() ? dgram.offset_ : off)
        {
            assert(offset_ <= dgram.len());
            memcpy(header_ + header_offset_,
                   dgram.header_ + dgram.header_offset(),
                   dgram.header_len());
        }

        /*!
         * @brief Destruct datagram
         */
        ~Datagram() { }

        void normalize()
        {
            const gu::SharedBuffer old_payload(payload_);
            payload_ = gu::SharedBuffer(new gu::Buffer);
            payload_->reserve(header_len() + old_payload->size() - offset_);

            if (header_len() > offset_)
            {
                payload_->insert(payload_->end(),
                                 header_ + header_offset_ + offset_,
                                 header_ + header_size_);
                offset_ = 0;
            }
            else
            {
                offset_ -= header_len();
            }
            header_offset_ = header_size_;
            payload_->insert(payload_->end(), old_payload->begin() + offset_,
                             old_payload->end());
            offset_ = 0;
        }

        gu::byte_t* header() { return header_; }
        const gu::byte_t* header() const { return header_; }
        size_t header_size() const { return header_size_; }
        size_t header_len() const { return (header_size_ - header_offset_); }
        size_t header_offset() const { return header_offset_; }
        void set_header_offset(const size_t off)
        {
            // assert(off <= header_size_);
            if (off > header_size_) gu_throw_fatal << "out of hdrspace";
            header_offset_ = off;
        }

        const gu::Buffer& payload() const
        {
            assert(payload_ != 0);
            return *payload_;
        }
        gu::Buffer& payload()
        {
            assert(payload_ != 0);
            return *payload_;
        }
        size_t len() const { return (header_size_ - header_offset_ + payload_->size()); }
        size_t offset() const { return offset_; }

    private:
        friend uint16_t crc16(const Datagram&, size_t);
        friend uint32_t crc32(const Datagram&, size_t);

        static const size_t header_size_ = 128;
        gu::byte_t          header_[header_size_];
        size_t              header_offset_;
        gu::SharedBuffer    payload_;
        size_t              offset_;
    };

    inline uint16_t crc16(const Datagram& dg, size_t offset = 0)
    {
        boost::crc_16_type crc;
        assert(offset < dg.len());
        gu::byte_t lenb[4];
        gu::serialize4(static_cast<int32_t>(dg.len() - offset), lenb, sizeof(lenb), 0);
        crc.process_block(lenb, lenb + sizeof(lenb));
        if (offset < dg.header_len())
        {
            crc.process_block(dg.header_ + dg.header_offset_ + offset,
                              dg.header_ + dg.header_size_);
            offset = 0;
        }
        else
        {
            offset -= dg.header_len();
        }
        crc.process_block(&(*dg.payload_)[0] + offset,
                          &(*dg.payload_)[0] + dg.payload_->size());
        return crc.checksum();
    }

    inline uint32_t crc32(const Datagram& dg, size_t offset = 0)
    {
        boost::crc_32_type crc;
        gu::byte_t lenb[4];
        gu::serialize4(static_cast<int32_t>(dg.len() - offset), lenb, sizeof(lenb), 0);
        crc.process_block(lenb, lenb + sizeof(lenb));
        if (offset < dg.header_len())
        {
            crc.process_block(dg.header_ + dg.header_offset_ + offset,
                              dg.header_ + dg.header_size_);
            offset = 0;
        }
        else
        {
            offset -= dg.header_len();
        }
        crc.process_block(&(*dg.payload_)[0] + offset,
                          &(*dg.payload_)[0] + dg.payload_->size());
        return crc.checksum();
    }
}

#endif // GCOMM_DATAGRAM_HPP
