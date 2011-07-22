//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//

#ifndef GALERA_KEY_HPP
#define GALERA_KEY_HPP

#include "serialization.hpp"
#include "wsrep_api.h"

#include "gu_unordered.hpp"
#include "gu_throw.hpp"
#include "gu_vlq.hpp"

#include <deque>
#include <iomanip>
#include <iterator>

#include <cstring>
#include <stdint.h>

namespace galera
{

    // helper to cast from any kind of pointer to void
    template <typename C>
    static inline void* void_cast(const C* c)
    {
        return const_cast<void*>(reinterpret_cast<const void*>(c));
    }


    class KeyPart0
    {
    public:
        KeyPart0(const gu::byte_t* key) : key_(key) { }
        const gu::byte_t* buf() const { return key_; }
        size_t size()           const { return (1 + key_[0]); }
        size_t key_len()        const { return key_[0]      ; }
        const gu::byte_t* key() const { return &key_[1]     ; }
        bool operator==(const KeyPart0& other) const
        {
            return (key_[0] == other.key_[0] &&
                    memcmp(key_ + 1, other.key_ + 1, key_[0]) == 0);
        }
    private:
        const gu::byte_t* key_;
    };


    inline std::ostream& operator<<(std::ostream& os, const KeyPart0& kp)
    {
        const std::ostream::fmtflags prev_flags(os.flags(std::ostream::hex));
        const char                   prev_fill(os.fill('0'));

        for (const gu::byte_t* i(kp.key()); i != kp.key() + kp.key_len();
             ++i)
        {
            os << std::setw(2) << static_cast<int>(*i);
        }
        os.flags(prev_flags);
        os.fill(prev_fill);

        return os;
    }


    class KeyPart1
    {
    public:
        KeyPart1(const gu::byte_t* buf, size_t buf_size)
            :
            buf_(buf),
            buf_size_(buf_size)
        { }
        const gu::byte_t* buf() const { return buf_; }
        size_t size() const { return buf_size_; }
        size_t key_len() const
        {
            size_t ret;
            (void)gu::uleb128_decode(buf_, buf_size_, 0, ret);
            return ret;
        }
        const gu::byte_t* key() const { return buf_ + key_len(); }
        bool operator==(const KeyPart1& other) const
        {
            return (other.buf_size_ == buf_size_ &&
                    memcmp(other.buf_, buf_, buf_size_) == 0);
        }
    private:
        const gu::byte_t* buf_;
        size_t            buf_size_;
    };


    inline std::ostream& operator<<(std::ostream& os, const KeyPart1& kp)
    {
        const std::ostream::fmtflags prev_flags(os.flags(std::ostream::hex));
        const char                   prev_fill(os.fill('0'));

        for (const gu::byte_t* i(kp.key()); i != kp.key() + kp.key_len();
             ++i)
        {
            os << std::setw(2) << static_cast<int>(*i);
        }
        os.flags(prev_flags);
        os.fill(prev_fill);

        return os;
    }


    class Key
    {
    public:
        Key(int version) : version_(version), keys_() { }

        Key(int version, const wsrep_key_t* keys, size_t keys_len)
            :
            version_(version),
            keys_   ()
        {
            if (keys_len > 255)
            {
                gu_throw_error(EINVAL)
                    << "maximum number of key parts exceeded: " << keys_len;
            }

            switch (version)
            {
            case 0:
                for (size_t i(0); i < keys_len; ++i)
                {
                    if (keys[i].key_len > 256)
                    {
                        gu_throw_error(EINVAL)
                            << "key part length " << keys[i].key_len
                            << " greater than max 256";
                    }
                    gu::byte_t len(keys[i].key_len);
                    const gu::byte_t* base(reinterpret_cast<const gu::byte_t*>(
                                               keys[i].key));
                    keys_.push_back(len);
                    keys_.insert(keys_.end(), base, base + len);
                }
                break;
            case 1:
                for (size_t i(0); i < keys_len; ++i)
                {
                    size_t len_size(gu::uleb128_size(keys[i].key_len));
                    size_t offset(keys_.size());
                    keys_.resize(offset + len_size);
                    (void)gu::uleb128_encode(
                        keys[i].key_len, &keys_[0], keys_.size(), offset);
                    const gu::byte_t* base(reinterpret_cast<const gu::byte_t*>(
                                               keys[i].key));
                    keys_.insert(keys_.end(), base, base + keys[i].key_len);
                }
                break;
            default:
                gu_throw_fatal << "unsupported key version: " << version_;
                throw;
            }
        }

        template <class Ci>
        Key(int version, Ci begin, Ci end) : version_(version), keys_()
        {

            for (Ci i(begin); i != end; ++i)
            {
                keys_.insert(
                    keys_.end(), i->buf(), i->buf() + i->size());
            }
        }

        int version() const { return version_; }

        template <class C>
        C key_parts0() const
        {
            C ret;
            size_t i;
            for (i = 0; i < keys_.size(); )
            {
                KeyPart0 kp(&keys_[i]);
                ret.push_back(kp);
                i += kp.size();
            }
            if (i != keys_.size()) gu_throw_fatal;
            return ret;
        }

        template <class C>
        C key_parts1() const
        {
            C ret;
            size_t i;
            for (i = 0; i < keys_.size(); )
            {
                size_t key_len;
                size_t offset(
                    gu::uleb128_decode(
                        &keys_[0], keys_.size(), i, key_len));
                KeyPart1 kp(&keys_[0] + i, key_len + (offset - i));
                ret.push_back(kp);
                i += kp.size();
            }
            if (i != keys_.size()) gu_throw_fatal;
            return ret;
        }


        bool operator==(const Key& other) const
        {
            return (keys_ == other.keys_);
        }

    private:
        friend class KeyHash;
        friend size_t serialize(const Key&, gu::byte_t*, size_t, size_t);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, Key&);
        friend size_t serial_size(const Key&);
        friend std::ostream& operator<<(std::ostream& os, const Key& key);
        int        version_;
        gu::Buffer keys_;
    };

    inline std::ostream& operator<<(std::ostream& os, const Key& key)
    {
        switch (key.version_)
        {
        case 0:
        {
            std::deque<KeyPart0> dq(key.key_parts0<std::deque<KeyPart0> >());
            std::copy(dq.begin(), dq.end(),
                      std::ostream_iterator<KeyPart0>(os, " "));
            break;
        }
        case 1:
        {
            std::deque<KeyPart1> dq(key.key_parts1<std::deque<KeyPart1> >());
            std::copy(dq.begin(), dq.end(),
                      std::ostream_iterator<KeyPart1>(os, " "));
            break;
        }
        default:
            gu_throw_fatal << "unsupported key version: " << key.version_;
            throw;
        }
        return os;
    }


    class KeyHash
    {
    public:
        size_t operator()(const Key& k) const
        {
            size_t prime(5381);
            for (gu::Buffer::const_iterator i(k.keys_.begin());
                 i != k.keys_.end(); ++i)
            {
                prime = ((prime << 5) + prime) + *i;
            }
            return prime;
        }
    };

    inline size_t serialize(const Key& key, gu::byte_t* buf, size_t buflen,
                            size_t offset)
    {
        switch (key.version_)
        {
        case 0:
            return serialize<uint16_t>(key.keys_, buf, buflen, offset);
        case 1:
            offset = gu::uleb128_encode(key.keys_.size(), buf, buflen, offset);
            if (offset + key.keys_.size() > buflen) gu_throw_fatal;
            std::copy(&key.keys_[0], &key.keys_[0] + key.keys_.size(),
                      buf + offset);
            return (offset + key.keys_.size());
        default:
            gu_throw_fatal << "unsupported key version: " << key.version_;
            throw;
        }
    }

    inline size_t unserialize(const gu::byte_t* buf, size_t buflen,
                              size_t offset, Key& key)
    {
        switch (key.version_)
        {
        case 0:
            return unserialize<uint16_t>(buf, buflen, offset, key.keys_);
        case 1:
        {
            size_t len;
            offset = gu::uleb128_decode(buf, buflen, offset, len);
            key.keys_.resize(len);
            std::copy(buf + offset, buf + offset + len, key.keys_.begin());
            return (offset + len);
        }
        default:
            gu_throw_fatal << "unsupported key version: " << key.version_;
            throw;
        }
    }

    inline size_t serial_size(const Key& key)
    {
        switch (key.version_)
        {
        case 0:
            return serial_size<uint16_t>(key.keys_);
        case 1:
        {
            size_t size(gu::uleb128_size(key.keys_.size()));
            return (size + key.keys_.size());
        }
        default:
            gu_throw_fatal << "unsupported key version: " << key.version_;
            throw;
        }
    }

}


#endif // GALERA_KEY_HPP