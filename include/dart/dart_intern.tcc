#ifndef DART_INTERN_IMPL_H
#define DART_INTERN_IMPL_H

/*----- Local Includes -----*/

#include "dart_intern.h"

/*----- Function Implementations -----*/

namespace dart {

  namespace detail {

    template <size_t bytes>
    int prefix_compare_impl(char const* prefix, char const* str, size_t const len) noexcept {
      if (len && *prefix != *str) return *prefix - *str;
      else if (len) return prefix_compare_impl<bytes - 1>(prefix + 1, str + 1, len - 1);
      else return *prefix;
    }
    template <>
    inline int prefix_compare_impl<0>(char const*, char const* str, size_t const len) noexcept {
      if (len) return -*str;
      else return 0;
    }

#ifdef DART_USE_SAJSON
    // FIXME: Find somewhere better to put these functions.
    template <template <class> class RefCount>
    raw_type json_identify(sajson::value curr_val) {
      switch (curr_val.get_type()) {
        case sajson::TYPE_OBJECT:
          return raw_type::object;
        case sajson::TYPE_ARRAY:
          return raw_type::array;
        case sajson::TYPE_STRING:
          // Figure out what type of string we've been given.
          return identify_string<RefCount>({curr_val.as_cstring(), curr_val.get_string_length()});
        case sajson::TYPE_INTEGER:
          return identify_integer(curr_val.get_integer_value());
        case sajson::TYPE_DOUBLE:
          return identify_decimal(curr_val.get_double_value());
        case sajson::TYPE_FALSE:
        case sajson::TYPE_TRUE:
          return raw_type::boolean;
        default:
          DART_ASSERT(curr_val.get_type() == sajson::TYPE_NULL);
          return raw_type::null;
      }
    }

    template <template <class> class RefCount>
    size_t json_lower(gsl::byte* buffer, sajson::value curr_val) {
      auto raw = json_identify<RefCount>(curr_val);
      switch (raw) {
        case raw_type::object:
          new(buffer) object<RefCount>(curr_val);
          break;
        case raw_type::array:
          new(buffer) array<RefCount>(curr_val);
          break;
        case raw_type::small_string:
        case raw_type::string:
          new(buffer) string(curr_val.as_cstring(), curr_val.get_string_length());
          break;
        case raw_type::big_string:
          new(buffer) big_string(curr_val.as_cstring(), curr_val.get_string_length());
          break;
        case raw_type::short_integer:
          new(buffer) primitive<int16_t>(curr_val.get_integer_value());
          break;
        case raw_type::integer:
          new(buffer) primitive<int32_t>(curr_val.get_integer_value());
          break;
        case raw_type::long_integer:
          new(buffer) primitive<int64_t>(curr_val.get_integer_value());
          break;
        case raw_type::decimal:
          new(buffer) primitive<float>(curr_val.get_double_value());
          break;
        case raw_type::long_decimal:
          new(buffer) primitive<double>(curr_val.get_double_value());
          break;
        case raw_type::boolean:
          new(buffer) detail::primitive<bool>((curr_val.get_type() == sajson::TYPE_TRUE) ? true : false);
          break;
        default:
          DART_ASSERT(curr_val.get_type() == sajson::TYPE_NULL);
          break;
      }
      return detail::find_sizeof<RefCount>({raw, buffer});
    }
#endif

#if DART_HAS_RAPIDJSON
    // FIXME: Find somewhere better to put these functions.
    template <template <class> class RefCount>
    raw_type json_identify(rapidjson::Value const& curr_val) {
      switch (curr_val.GetType()) {
        case rapidjson::kObjectType:
          return raw_type::object;
        case rapidjson::kArrayType:
          return raw_type::array;
        case rapidjson::kStringType:
          // Figure out what type of string we've been given.
          return identify_string<RefCount>({curr_val.GetString(), curr_val.GetStringLength()});
        case rapidjson::kNumberType:
          // Figure out what type of number we've been given.
          if (curr_val.IsInt()) return identify_integer(curr_val.GetInt64());
          else return identify_decimal(curr_val.GetDouble());
        case rapidjson::kTrueType:
        case rapidjson::kFalseType:
          return raw_type::boolean;
        default:
          DART_ASSERT(curr_val.IsNull());
          return raw_type::null;
      }
    }

    template <template <class> class RefCount>
    size_t json_lower(gsl::byte* buffer, rapidjson::Value const& curr_val) {
      auto raw = json_identify<RefCount>(curr_val);
      switch (raw) {
        case raw_type::object:
          new(buffer) object<RefCount>(curr_val);
          break;
        case raw_type::array:
          new(buffer) array<RefCount>(curr_val);
          break;
        case raw_type::small_string:
        case raw_type::string:
          new(buffer) string(curr_val.GetString(), curr_val.GetStringLength());
          break;
        case raw_type::big_string:
          new(buffer) big_string(curr_val.GetString(), curr_val.GetStringLength());
          break;
        case raw_type::short_integer:
          new(buffer) primitive<int16_t>(curr_val.GetInt());
          break;
        case raw_type::integer:
          new(buffer) primitive<int32_t>(curr_val.GetInt());
          break;
        case raw_type::long_integer:
          new(buffer) primitive<int64_t>(curr_val.GetInt64());
          break;
        case raw_type::decimal:
          new(buffer) primitive<float>(curr_val.GetFloat());
          break;
        case raw_type::long_decimal:
          new(buffer) primitive<double>(curr_val.GetDouble());
          break;
        case raw_type::boolean:
          new(buffer) detail::primitive<bool>(curr_val.GetBool());
          break;
        default:
          DART_ASSERT(curr_val.IsNull());
          break;
      }
      return detail::find_sizeof<RefCount>({raw, buffer});
    }
#endif

  }

  template <template <class> class RefCount>
  basic_buffer<RefCount>::basic_buffer(detail::raw_element raw, buffer_ref_type ref) :
    raw(raw),
    buffer_ref(std::move(ref))
  {
    if (is_null()) buffer_ref.reset();
  }

  template <template <class> class RefCount>
  auto basic_buffer<RefCount>::allocate_pointer(gsl::span<gsl::byte const> buffer) const -> buffer_ref_type {
    if (buffer.empty()) throw std::invalid_argument("dart::packet buffer must not be empty");
    
    // Allocate an aligned region.
    gsl::byte* tmp;
    int retval = posix_memalign(reinterpret_cast<void**>(&tmp),
        detail::alignment_of<RefCount>(detail::raw_type::object), buffer.size());
    if (retval) throw std::bad_alloc();

    // Copy and return.
    buffer_ref_type ref {tmp, [] (gsl::byte const* ptr) { free(const_cast<gsl::byte*>(ptr)); }};
    std::copy(buffer.begin(), buffer.end(), tmp);
    return ref;
  }

  template <template <class> class RefCount>
  template <class Pointer>
  Pointer&& basic_buffer<RefCount>::validate_pointer(Pointer&& ptr) const {
    // Validate arguments.
    if (!ptr) {
      throw std::invalid_argument("dart::packet pointer must not be null.");
    } else if (detail::align_pointer<RefCount>(ptr.get(), detail::raw_type::object) != ptr.get()) {
      throw std::invalid_argument("dart::packet pointer must be aligned to a 64-bit word boundary");
    }
    return std::forward<Pointer>(ptr);
  }

  template <template <class> class RefCount>
  template <class Pointer>
  auto basic_buffer<RefCount>::normalize(Pointer ptr) const -> buffer_ref_type {
    // This whole business is necessary to allow std::unique_ptr<gsl::byte const[]>
    // to convert to std::shared_ptr<gsl::byte const>, which the STL doesn't define
    // a conversion for, but is convenient for this library.
    buffer_ref_type tmp(ptr.get(), std::move(ptr.get_deleter()));
    ptr.release();
    return tmp;
  }

  template <template <class> class RefCount>
  void basic_heap<RefCount>::copy_on_write(size_type overcount) {
    if (refcount() > overcount) {
      if (is_object()) data = fields_type(new packet_fields(get_fields()));
      else if (is_array()) data = elements_type(new packet_elements(get_elements()));
    }
  }

  // FIXME: Audit this function. A LOT has changed since it was written.
  template <template <class> class RefCount>
  auto basic_heap<RefCount>::upper_bound() const -> size_type {
    switch (get_raw_type()) {
      case detail::raw_type::object:
        {
          // Start with the base size of the object structure, then add the size of our vtable.
          // The plus one is to account for any potentially required padding.
          auto* fields = try_get_fields();
          size_t max = sizeof(detail::object<RefCount>) + ((sizeof(detail::object_entry) * (fields->size() + 1)));

          // Now iterate over our fields and calculate the max memory required for each.
          for (auto& field : *fields) {
            // Get the maximum size of both our key and value.
            size_t key_max = field.first.upper_bound(), val_max = field.second.upper_bound();

            // Total size required for this field is the max size of the key, plus the maximum required
            // padding for the value type (minus 1), plus the max size of the value, plus the maximum
            // required padding for a subsequent key (minus 1).
            max += key_max + detail::alignment_of<RefCount>(field.second.get_raw_type()) - 1;
            max += val_max + detail::alignment_of<RefCount>(detail::raw_type::string) - 1;
          }

          // This is required so that packets can be copied into contiguous buffers
          // without ruining their alignment.
          max = detail::pad_bytes<RefCount>(max, detail::raw_type::object);

          // Make sure we aren't going to exceed the maximum offset value we can encode in our vtable.
          if (max > max_aggregate_size) {
            throw std::length_error("Offset required for encoding is too large for dart::packet vtable");
          }
          return max;
        }
      case detail::raw_type::array:
        {
          // Start with the base size of the array structure, then add the size of our vtable.
          // The plus one is to account for any potentially required padding.
          auto* elements = try_get_elements();
          size_t max = sizeof(detail::array<RefCount>) + (sizeof(detail::array_entry) * (elements->size() + 1));

          // Now iterate over each element and add their max size.
          // Max size for each element is considered to be their reported maximum size, plus the maximum required
          // padding for the next element.
          for (auto& elem : *elements) {
            max += elem.upper_bound() + detail::alignment_of<RefCount>(elem.get_raw_type()) - 1;
          }

          // Make sure we aren't going to exceed the maximum offset value we can encode in our vtable.
          if (max > max_aggregate_size) {
            throw std::length_error("Offset required for encoding is too large for dart::packet vtable");
          }
          return max;
        }
      case detail::raw_type::small_string:
      case detail::raw_type::string:
        // Maximum size in the case of a string is the base size of the string structure, plus the
        // length of the string. Structure size includes null-terminating character.
        return detail::string::static_sizeof(size());
      case detail::raw_type::big_string:
        // Maximum size in the case of a string is the base size of the string structure, plus the
        // length of the string. Structure size includes null-terminating character.
        return detail::big_string::static_sizeof(size());
      case detail::raw_type::short_integer:
        return detail::primitive<int16_t>::static_sizeof();
      case detail::raw_type::integer:
        return detail::primitive<int32_t>::static_sizeof();
      case detail::raw_type::long_integer:
        return detail::primitive<int64_t>::static_sizeof();
      case detail::raw_type::decimal:
        return detail::primitive<float>::static_sizeof();
      case detail::raw_type::long_decimal:
        return detail::primitive<double>::static_sizeof();
      case detail::raw_type::boolean:
        return detail::primitive<bool>::static_sizeof();
      default:
        DART_ASSERT(get_raw_type() == detail::raw_type::null);
        return 0;
    }
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::layout(gsl::byte* buffer) const noexcept -> size_type {
    // Construct a wrapper class of the correct type in the provided buffer, and return the number
    // of bytes used.
    auto raw = get_raw_type();
    switch (raw) {
      case detail::raw_type::object:
        new(buffer) detail::object<RefCount>(try_get_fields());
        break;
      case detail::raw_type::array:
        new(buffer) detail::array<RefCount>(try_get_elements());
        break;
      case detail::raw_type::small_string:
      case detail::raw_type::string:
        {
          auto view = strv();
          new(buffer) detail::string(view.data(), view.size());
          break;
        }
      case detail::raw_type::big_string:
        {
          auto view = strv();
          new(buffer) detail::big_string(view.data(), view.size());
          break;
        }
      case detail::raw_type::short_integer:
        new(buffer) detail::primitive<int16_t>(integer());
        break;
      case detail::raw_type::integer:
        new(buffer) detail::primitive<int32_t>(integer());
        break;
      case detail::raw_type::long_integer:
        new(buffer) detail::primitive<int64_t>(integer());
        break;
      case detail::raw_type::decimal:
        new(buffer) detail::primitive<float>(decimal());
        break;
      case detail::raw_type::long_decimal:
        new(buffer) detail::primitive<double>(decimal());
        break;
      case detail::raw_type::boolean:
        new(buffer) detail::primitive<bool>(boolean());
        break;
      default:
        DART_ASSERT(raw == detail::raw_type::null);
        break;
    }
    return detail::find_sizeof<RefCount>({raw, buffer});
  }

  template <template <class> class RefCount>
  detail::raw_type basic_heap<RefCount>::get_raw_type() const noexcept {
    switch (get_type()) {
      case type::object:
        return detail::raw_type::object;
      case type::array:
        return detail::raw_type::array;
      case type::string:
        return detail::identify_string<RefCount>(strv());
      case type::integer:
        return detail::identify_integer(integer());
      case type::decimal:
        return detail::identify_decimal(decimal());
      case type::boolean:
        return detail::raw_type::boolean;
      default:
        DART_ASSERT(is_null());
        return detail::raw_type::null;
    }
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::get_fields() -> packet_fields& {
    if (is_object()) return *shim::get<fields_type>(data);
    else throw type_error("dart::heap is not an object and cannot access fields");
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::get_fields() const -> packet_fields const& {
    if (is_object()) return *shim::get<fields_type>(data);
    else throw type_error("dart::heap is not an object and cannot access fields");
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::try_get_fields() noexcept -> packet_fields* {
    if (is_object()) return shim::get<fields_type>(data).get();
    else return nullptr;
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::try_get_fields() const noexcept -> packet_fields const* {
    if (is_object()) return shim::get<fields_type>(data).get();
    else return nullptr;
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::get_elements() -> packet_elements& {
    if (is_array()) return *shim::get<elements_type>(data);
    else throw type_error("dart::heap is not an array and cannot access elements");
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::get_elements() const -> packet_elements const& {
    if (is_array()) return *shim::get<elements_type>(data);
    else throw type_error("dart::heap is not an object and cannot access elements");
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::try_get_elements() noexcept -> packet_elements* {
    if (is_array()) return shim::get<elements_type>(data).get();
    else return nullptr;
  }

  template <template <class> class RefCount>
  auto basic_heap<RefCount>::try_get_elements() const noexcept -> packet_elements const* {
    if (is_array()) return shim::get<elements_type>(data).get();
    else return nullptr;
  }

  template <template <class> class RefCount>
  basic_heap<RefCount>& basic_packet<RefCount>::get_heap() {
    if (!is_finalized()) return shim::get<basic_heap<RefCount>>(impl);
    else throw state_error("dart::packet is finalized and cannot access a heap representation");
  }

  template <template <class> class RefCount>
  basic_heap<RefCount> const& basic_packet<RefCount>::get_heap() const {
    if (!is_finalized()) return shim::get<basic_heap<RefCount>>(impl);
    else throw state_error("dart::packet is finalized and cannot access a heap representation");
  }

  template <template <class> class RefCount>
  basic_heap<RefCount>* basic_packet<RefCount>::try_get_heap() {
    return shim::get_if<basic_heap<RefCount>>(&impl);
  }

  template <template <class> class RefCount>
  basic_heap<RefCount> const* basic_packet<RefCount>::try_get_heap() const {
    return shim::get_if<basic_heap<RefCount>>(&impl);
  }

  template <template <class> class RefCount>
  basic_buffer<RefCount>& basic_packet<RefCount>::get_buffer() {
    if (is_finalized()) return shim::get<basic_buffer<RefCount>>(impl);
    else throw state_error("dart::packet is not finalized and cannot access a buffer representation");
  }

  template <template <class> class RefCount>
  basic_buffer<RefCount> const& basic_packet<RefCount>::get_buffer() const {
    if (is_finalized()) return shim::get<basic_buffer<RefCount>>(impl);
    else throw state_error("dart::packet is not finalized and cannot access a buffer representation");
  }

  template <template <class> class RefCount>
  basic_buffer<RefCount>* basic_packet<RefCount>::try_get_buffer() {
    return shim::get_if<basic_buffer<RefCount>>(&impl);
  }

  template <template <class> class RefCount>
  basic_buffer<RefCount> const* basic_packet<RefCount>::try_get_buffer() const {
    return shim::get_if<basic_buffer<RefCount>>(&impl);
  }

  namespace detail {

    template <template <class> class RefCount>
    bool map_comparator<RefCount>::operator ()(basic_heap<RefCount> const& lhs, shim::string_view rhs) const {
      auto const lhs_size = lhs.size();
      auto const rhs_size = rhs.size();
      if (lhs_size != rhs_size) return lhs_size < rhs_size;
      else return lhs.strv() < rhs;
    }

    template <template <class> class RefCount>
    bool map_comparator<RefCount>::operator ()(shim::string_view lhs, basic_heap<RefCount> const& rhs) const {
      auto const lhs_size = lhs.size();
      auto const rhs_size = rhs.size();
      if (lhs_size != rhs_size) return lhs_size < rhs_size;
      else return lhs < rhs.strv();
    }

    template <template <class> class RefCount>
    bool map_comparator<RefCount>::operator ()(basic_heap<RefCount> const& lhs, basic_heap<RefCount> const& rhs) const {
      auto const lhs_size = lhs.size();
      auto const rhs_size = rhs.size();
      if (lhs_size != rhs_size) return lhs_size < rhs_size;
      return lhs.strv() < rhs.strv();
    }

    template <class Lhs, class Rhs>
    bool typeless_comparator::operator ()(Lhs&& lhs, Rhs&& rhs) const noexcept {
      // if constexpr!
      // my kingdom for if constexpr!
      return shim::compose_together(
        [] (auto&& l, auto&& r, std::true_type) {
          return std::forward<decltype(l)>(l) == std::forward<decltype(r)>(r);
        },
        [] (auto&&, auto&&, std::false_type) {
          return false;
        }
      )(std::forward<Lhs>(lhs), std::forward<Rhs>(rhs), meta::are_comparable<Lhs, Rhs> {});
    }

    template <class T>
    vtable_entry<T>::vtable_entry(detail::raw_type type, uint32_t offset) {
      // Truncate dynamic type information.
      if (type == detail::raw_type::small_string) type = detail::raw_type::string;

      // Create our combined entry for the vtable.
      layout.offset = offset;
      layout.type = static_cast<uint8_t>(type);
    }

    template <class T>
    raw_type vtable_entry<T>::get_type() const noexcept {
      // Apparently this CAN'T use brace-initialization for... REASONS???
      // Put it down as _yet another_ painful edge case for "uniform" initialization.
      // I'm asking for an explicit, non-narrowing, conversion either way,
      // don't really understand the issue.
      return raw_type(layout.type.get());
    }

    template <class T>
    uint32_t vtable_entry<T>::get_offset() const noexcept {
      return layout.offset;
    }

    inline prefix_entry::prefix_entry(detail::raw_type type, uint32_t offset, shim::string_view prefix) noexcept :
      vtable_entry<prefix_entry>(type, offset)
    {
      // Decide how many bytes we're going to copy out of the key.
      auto bytes = prefix.size();
      if (bytes > sizeof(this->layout.prefix)) bytes = sizeof(this->layout.prefix);

      // Set the length, truncating down to 256.
      auto max_len = std::numeric_limits<uint8_t>::max();
      if (prefix.size() < max_len) this->layout.len = prefix.size();
      else this->layout.len = max_len;

      // Try SO HARD not to violate strict aliasing rules, while copying those characters into an integer.
      // This is probably all still undefined behavior.
      // FIXME: It is because, _officially speaking_, we're reading an unitialized integer.
      storage_t raw {};
      std::copy_n(prefix.data(), bytes, reinterpret_cast<char*>(&raw));
      new(&raw) prefix_type;
      this->layout.prefix = *shim::launder(reinterpret_cast<prefix_type const*>(&raw));
    }

    inline int prefix_entry::prefix_compare(shim::string_view str) const noexcept {
      // Cache all of our lengths and stuff.
      auto const their_len = str.size();
      auto const our_len = this->layout.len;
      constexpr auto max_len = std::numeric_limits<uint8_t>::max();

      // Compare first by string lengths, then by lexical ordering.
      // If they are longer than us, but we're capped at the max value,
      // return equality to force key lookup to fall back on the general case.
      if (our_len < their_len) return (our_len == max_len) ? 0 : -1;
      else if (our_len == their_len) return compare_impl(str.data(), their_len);
      else return 1;
    }

    inline int prefix_entry::compare_impl(char const* const str, size_t const len) const noexcept {
      // Fast path where we attempt to perform a direct integer comparison.
      if (len >= sizeof(prefix_type)) {
        // Despite all my hard work, this is probably still undefined behavior.
        // FIXME: It is because, _officially speaking_, we're reading an unitialized integer.
        storage_t raw {};
        std::copy_n(str, sizeof(this->layout.prefix), reinterpret_cast<char*>(&raw));
        new(&raw) prefix_type;
        if (*shim::launder(reinterpret_cast<prefix_type const*>(&raw)) == this->layout.prefix) {
          return 0;
        }
      }

      // Fallback path where we actually compare the prefixes.
      auto* bytes = reinterpret_cast<char const*>(&this->layout.prefix);
      return detail::prefix_compare_impl<sizeof(prefix_type)>(bytes, str, len);
    }

  }

}

#endif
