/*
 * Copyright 2021 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Firestore/core/src/model/value_util.h"
#include "Firestore/core/src/model/database_id.h"
#include "Firestore/core/src/model/field_value.h"
#include "Firestore/core/src/nanopb/message.h"
#include "Firestore/core/src/remote/serializer.h"
#include "Firestore/core/src/util/comparison.h"
#include "Firestore/core/test/unit/testutil/equals_tester.h"
#include "Firestore/core/test/unit/testutil/testutil.h"
#include "Firestore/core/test/unit/testutil/time_testing.h"
#include "absl/base/casts.h"
#include "gtest/gtest.h"

namespace firebase {
namespace firestore {
namespace model {
namespace {

using testutil::Array;
using testutil::BlobValue;
using testutil::DbId;
using testutil::Key;
using testutil::Map;
using testutil::time_point;
using testutil::Value;
using util::ComparisonResult;

double ToDouble(uint64_t value) {
  return absl::bit_cast<double>(value);
}

const uint64_t kNanBits = 0x7fff000000000000ULL;

const time_point kDate1 = testutil::MakeTimePoint(2016, 5, 20, 10, 20, 0);
const Timestamp kTimestamp1{1463739600, 0};

const time_point kDate2 = testutil::MakeTimePoint(2016, 10, 21, 15, 32, 0);
const Timestamp kTimestamp2{1477063920, 0};

class ValueUtilTest : public ::testing::Test {
 public:
  template <typename T>
  google_firestore_v1_Value Wrap(T input) {
    model::FieldValue fv = Value(input);
    return serializer.EncodeFieldValue(fv);
  }

  template <typename... Args>
  google_firestore_v1_Value WrapObject(Args&&... key_value_pairs) {
    FieldValue fv =
        testutil::WrapObject(std::forward<Args>(key_value_pairs)...);
    return serializer.EncodeFieldValue(fv);
  }

  template <typename... Args>
  google_firestore_v1_Value WrapArray(Args&&... values) {
    std::vector<model::FieldValue> contents{
        Value(std::forward<Args>(values))...};
    FieldValue fv = FieldValue::FromArray(std::move(contents));
    return serializer.EncodeFieldValue(fv);
  }

  google_firestore_v1_Value WrapReference(const DatabaseId& database_id,
                                          const DocumentKey& key) {
    google_firestore_v1_Value result{};
    result.which_value_type = google_firestore_v1_Value_reference_value_tag;
    result.reference_value =
        serializer.EncodeResourceName(database_id, key.path());
    return result;
  }

  google_firestore_v1_Value WrapServerTimestamp(
      const model::FieldValue& input) {
    // TODO(mrschmidt): Replace with EncodeFieldValue encoding when available
    return WrapObject("__type__", "server_timestamp", "__local_write_time__",
                      input.server_timestamp_value().local_write_time());
  }

  template <typename... Args>
  void Add(std::vector<std::vector<google_firestore_v1_Value>>& groups,
           Args... values) {
    std::vector<google_firestore_v1_Value> group{std::forward<Args>(values)...};
    groups.emplace_back(group);
  }

  void VerifyEquality(std::vector<google_firestore_v1_Value>& left,
                      std::vector<google_firestore_v1_Value>& right,
                      bool expected_equals) {
    for (const auto& val1 : left) {
      for (const auto& val2 : right) {
        if (expected_equals) {
          EXPECT_EQ(val1, val2);
        } else {
          EXPECT_NE(val1, val2);
        }
      }
    }
  }

  void VerifyOrdering(std::vector<google_firestore_v1_Value>& left,
                      std::vector<google_firestore_v1_Value>& right,
                      ComparisonResult expected_result) {
    for (const auto& val1 : left) {
      for (const auto& val2 : right) {
        EXPECT_EQ(expected_result, Compare(val1, val2))
            << "Order check failed for '" << CanonicalId(val1) << "' and '"
            << CanonicalId(val2) << "' (expected "
            << static_cast<int>(expected_result) << ")";
        EXPECT_EQ(util::ReverseOrder(expected_result), Compare(val2, val1))
            << "Reverse order check failed for '" << CanonicalId(val1)
            << "' and '" << CanonicalId(val2) << "' (expected "
            << static_cast<int>(util::ReverseOrder(expected_result)) << ")";
      }
    }
  }

  void VerifyCanonicalId(const google_firestore_v1_Value& value,
                         const std::string& expected_canonical_id) {
    std::string actual_canonical_id = CanonicalId(value);
    EXPECT_EQ(expected_canonical_id, actual_canonical_id);
  }

  void VerifyDeepClone(const google_firestore_v1_Value& value) {
    nanopb::Message<google_firestore_v1_Value> clone1;

    [&] {
      nanopb::Message<google_firestore_v1_Value> clone2{DeepClone(value)};
      EXPECT_EQ(value, *clone2);
      clone1 = nanopb::Message<google_firestore_v1_Value>{DeepClone(*clone2)};
    }();

    // `clone2` is destroyed at this point, but `clone1` should be still valid.
    EXPECT_EQ(value, *clone1);
  }

 private:
  remote::Serializer serializer{DbId()};
};

TEST_F(ValueUtilTest, Equality) {
  // Create a matrix that defines an equality group. The outer vector has
  // multiple rows and each row can have an arbitrary number of entries.
  // The elements within a row must equal each other, but not be equal
  // to all elements of other rows.
  std::vector<std::vector<google_firestore_v1_Value>> equals_group;

  Add(equals_group, Wrap(nullptr), Wrap(nullptr));
  Add(equals_group, Wrap(false), Wrap(false));
  Add(equals_group, Wrap(true), Wrap(true));
  Add(equals_group, Wrap(std::numeric_limits<double>::quiet_NaN()),
      Wrap(ToDouble(kCanonicalNanBits)), Wrap(ToDouble(kNanBits)),
      Wrap(std::nan("1")), Wrap(std::nan("2")));
  // -0.0 and 0.0 compare the same but are not equal.
  Add(equals_group, Wrap(-0.0));
  Add(equals_group, Wrap(0.0));
  Add(equals_group, Wrap(1), Wrap(1LL));
  // Doubles and Longs aren't equal (even though they compare same).
  Add(equals_group, Wrap(1.0), Wrap(1.0));
  Add(equals_group, Wrap(1.1), Wrap(1.1));
  Add(equals_group, Wrap(BlobValue(0, 1, 1)));
  Add(equals_group, Wrap(BlobValue(0, 1)));
  Add(equals_group, Wrap("string"), Wrap("string"));
  Add(equals_group, Wrap("strin"));
  Add(equals_group, Wrap(std::string("strin\0", 6)));
  // latin small letter e + combining acute accent
  Add(equals_group, Wrap("e\u0301b"));
  // latin small letter e with acute accent
  Add(equals_group, Wrap("\u00e9a"));
  Add(equals_group, Wrap(Timestamp::FromTimePoint(kDate1)), Wrap(kTimestamp1));
  Add(equals_group, Wrap(Timestamp::FromTimePoint(kDate2)), Wrap(kTimestamp2));
  // NOTE: ServerTimestampValues can't be parsed via Wrap().
  Add(equals_group,
      WrapServerTimestamp(FieldValue::FromServerTimestamp(kTimestamp1)),
      WrapServerTimestamp(FieldValue::FromServerTimestamp(kTimestamp1)));
  Add(equals_group,
      WrapServerTimestamp(FieldValue::FromServerTimestamp(kTimestamp2)));
  Add(equals_group, Wrap(GeoPoint(0, 1)), Wrap(GeoPoint(0, 1)));
  Add(equals_group, Wrap(GeoPoint(1, 0)));
  Add(equals_group, WrapReference(DbId(), Key("coll/doc1")),
      WrapReference(DbId(), Key("coll/doc1")));
  Add(equals_group, WrapReference(DbId(), Key("coll/doc2")));
  Add(equals_group, WrapReference(DbId("project/baz"), Key("coll/doc2")));
  Add(equals_group, WrapArray("foo", "bar"), WrapArray("foo", "bar"));
  Add(equals_group, WrapArray("foo", "bar", "baz"));
  Add(equals_group, WrapArray("foo"));
  Add(equals_group, WrapObject("bar", 1, "foo", 2),
      WrapObject("foo", 2, "bar", 1));
  Add(equals_group, WrapObject("bar", 2, "foo", 1));
  Add(equals_group, WrapObject("bar", 1));
  Add(equals_group, WrapObject("foo", 1));

  for (size_t i = 0; i < equals_group.size(); ++i) {
    for (size_t j = i; j < equals_group.size(); ++j) {
      VerifyEquality(equals_group[i], equals_group[j],
                     /* expected_equals= */ i == j);
    }
  }
}

TEST_F(ValueUtilTest, Ordering) {
  // Create a matrix that defines a comparison group. The outer vector has
  // multiple rows and each row can have an arbitrary number of entries.
  // The elements within a row must compare equal to each other, but order after
  // all elements in previous groups and before all elements in later groups.
  std::vector<std::vector<google_firestore_v1_Value>> comparison_groups;

  // null first
  Add(comparison_groups, Wrap(nullptr));

  // booleans
  Add(comparison_groups, Wrap(false));
  Add(comparison_groups, Wrap(true));

  // numbers
  Add(comparison_groups, Wrap(-1e20));
  Add(comparison_groups, Wrap(LLONG_MIN));
  Add(comparison_groups, Wrap(-0.1));
  // Zeros all compare the same.
  Add(comparison_groups, Wrap(-0.0), Wrap(0.0), Wrap(0L));
  Add(comparison_groups, Wrap(0.1));
  // Doubles and longs Compare() the same.
  Add(comparison_groups, Wrap(1.0), Wrap(1L));
  Add(comparison_groups, Wrap(LLONG_MAX));
  Add(comparison_groups, Wrap(1e20));

  // dates
  Add(comparison_groups, Wrap(kTimestamp1));
  Add(comparison_groups, Wrap(kTimestamp2));

  // server timestamps come after all concrete timestamps.
  // NOTE: server timestamps can't be parsed with Wrap().
  Add(comparison_groups,
      WrapServerTimestamp(FieldValue::FromServerTimestamp(kTimestamp1)));
  Add(comparison_groups,
      WrapServerTimestamp(FieldValue::FromServerTimestamp(kTimestamp2)));

  // strings
  Add(comparison_groups, Wrap(""));
  Add(comparison_groups, Wrap("\001\ud7ff\ue000\uffff"));
  Add(comparison_groups, Wrap("(╯°□°）╯︵ ┻━┻"));
  Add(comparison_groups, Wrap("a"));
  Add(comparison_groups, Wrap(std::string("abc\0 def", 8)));
  Add(comparison_groups, Wrap("abc def"));
  // latin small letter e + combining acute accent + latin small letter b
  Add(comparison_groups, Wrap("e\u0301b"));
  Add(comparison_groups, Wrap("æ"));
  // latin small letter e with acute accent + latin small letter a
  Add(comparison_groups, Wrap("\u00e9a"));

  // blobs
  Add(comparison_groups, Wrap(BlobValue()));
  Add(comparison_groups, Wrap(BlobValue(0)));
  Add(comparison_groups, Wrap(BlobValue(0, 1, 2, 3, 4)));
  Add(comparison_groups, Wrap(BlobValue(0, 1, 2, 4, 3)));
  Add(comparison_groups, Wrap(BlobValue(255)));

  // resource names
  Add(comparison_groups, WrapReference(DbId("p1/d1"), Key("c1/doc1")));
  Add(comparison_groups, WrapReference(DbId("p1/d1"), Key("c1/doc2")));
  Add(comparison_groups, WrapReference(DbId("p1/d1"), Key("c10/doc1")));
  Add(comparison_groups, WrapReference(DbId("p1/d1"), Key("c2/doc1")));
  Add(comparison_groups, WrapReference(DbId("p1/d2"), Key("c1/doc1")));
  Add(comparison_groups, WrapReference(DbId("p2/d1"), Key("c1/doc1")));

  // geo points
  Add(comparison_groups, Wrap(GeoPoint(-90, -180)));
  Add(comparison_groups, Wrap(GeoPoint(-90, 0)));
  Add(comparison_groups, Wrap(GeoPoint(-90, 180)));
  Add(comparison_groups, Wrap(GeoPoint(0, -180)));
  Add(comparison_groups, Wrap(GeoPoint(0, 0)));
  Add(comparison_groups, Wrap(GeoPoint(0, 180)));
  Add(comparison_groups, Wrap(GeoPoint(1, -180)));
  Add(comparison_groups, Wrap(GeoPoint(1, 0)));
  Add(comparison_groups, Wrap(GeoPoint(1, 180)));
  Add(comparison_groups, Wrap(GeoPoint(90, -180)));
  Add(comparison_groups, Wrap(GeoPoint(90, 0)));
  Add(comparison_groups, Wrap(GeoPoint(90, 180)));

  // arrays
  Add(comparison_groups, WrapArray("bar"));
  Add(comparison_groups, WrapArray("foo", 1));
  Add(comparison_groups, WrapArray("foo", 2));
  Add(comparison_groups, WrapArray("foo", "0"));

  // objects
  Add(comparison_groups, WrapObject("bar", 0));
  Add(comparison_groups, WrapObject("bar", 0, "foo", 1));
  Add(comparison_groups, WrapObject("foo", 1));
  Add(comparison_groups, WrapObject("foo", 2));
  Add(comparison_groups, WrapObject("foo", "0"));

  for (size_t i = 0; i < comparison_groups.size(); ++i) {
    for (size_t j = i; j < comparison_groups.size(); ++j) {
      VerifyOrdering(
          comparison_groups[i], comparison_groups[j],
          i == j ? ComparisonResult::Same : ComparisonResult::Ascending);
    }
  }
}

TEST_F(ValueUtilTest, CanonicalId) {
  VerifyCanonicalId(Wrap(nullptr), "null");
  VerifyCanonicalId(Wrap(true), "true");
  VerifyCanonicalId(Wrap(false), "false");
  VerifyCanonicalId(Wrap(1), "1");
  VerifyCanonicalId(Wrap(1.0), "1.0");
  VerifyCanonicalId(Wrap(Timestamp(30, 1000)), "time(30,1000)");
  VerifyCanonicalId(Wrap("a"), "a");
  VerifyCanonicalId(Wrap(std::string("a\0b", 3)), std::string("a\0b", 3));
  VerifyCanonicalId(Wrap(BlobValue(1, 2, 3)), "010203");
  VerifyCanonicalId(WrapReference(DbId("p1/d1"), Key("c1/doc1")), "c1/doc1");
  VerifyCanonicalId(Wrap(GeoPoint(30, 60)), "geo(30.0,60.0)");
  VerifyCanonicalId(WrapArray(1, 2, 3), "[1,2,3]");
  VerifyCanonicalId(WrapObject("a", 1, "b", 2, "c", "3"), "{a:1,b:2,c:3}");
  VerifyCanonicalId(WrapObject("a", Array("b", Map("c", GeoPoint(30, 60)))),
                    "{a:[b,{c:geo(30.0,60.0)}]}");
}

TEST_F(ValueUtilTest, DeepClone) {
  VerifyDeepClone(Wrap(nullptr));
  VerifyDeepClone(Wrap(true));
  VerifyDeepClone(Wrap(false));
  VerifyDeepClone(Wrap(1));
  VerifyDeepClone(Wrap(1.0));
  VerifyDeepClone(Wrap(Timestamp(30, 1000)));
  VerifyDeepClone(Wrap("a"));
  VerifyDeepClone(Wrap(std::string("a\0b", 3)));
  VerifyDeepClone(Wrap(BlobValue(1, 2, 3)));
  VerifyDeepClone(WrapReference(DbId("p1/d1"), Key("c1/doc1")));
  VerifyDeepClone(Wrap(GeoPoint(30, 60)));
  VerifyDeepClone(WrapArray(1, 2, 3));
  VerifyDeepClone(WrapObject("a", 1, "b", 2, "c", "3"));
  VerifyDeepClone(WrapObject("a", Array("b", Map("c", GeoPoint(30, 60)))));
}

}  // namespace

}  // namespace model
}  // namespace firestore
}  // namespace firebase
