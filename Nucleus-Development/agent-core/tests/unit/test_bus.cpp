#include <gtest/gtest.h>
#include "agent/envelope_serialization.hpp"
#include "agent/bus.hpp"
#include <string>

using namespace agent;

TEST(EnvelopeSerialization, RoundTrip) {
    Envelope original;
    original.topic = "test.topic";
    original.correlation_id = "123e4567-e89b-12d3-a456-426614174000";
    original.payload_json = R"({"key":"value","number":42})";
    original.ts_ms = 1731283200000;
    
    std::string json = serialize_envelope(original);
    
    Envelope deserialized;
    ASSERT_TRUE(deserialize_envelope(json, deserialized));
    
    EXPECT_EQ(original.topic, deserialized.topic);
    EXPECT_EQ(original.correlation_id, deserialized.correlation_id);
    EXPECT_EQ(original.payload_json, deserialized.payload_json);
    EXPECT_EQ(original.ts_ms, deserialized.ts_ms);
}

TEST(EnvelopeSerialization, CorrelationIdPreservation) {
    Envelope req;
    req.topic = "ext.sample.echo";
    req.correlation_id = "550e8400-e29b-41d4-a716-446655440000";
    req.payload_json = R"({"message":"test"})";
    req.ts_ms = 1731283200000;
    
    std::string json = serialize_envelope(req);
    
    Envelope reply;
    ASSERT_TRUE(deserialize_envelope(json, reply));
    
    EXPECT_EQ(req.correlation_id, reply.correlation_id);
}

TEST(EnvelopeSerialization, ComplexPayload) {
    Envelope envelope;
    envelope.topic = "complex.topic";
    envelope.correlation_id = "test-id";
    envelope.payload_json = R"({"nested":{"array":[1,2,3],"object":{"key":"value"}}})";
    envelope.ts_ms = 1234567890;
    
    std::string json = serialize_envelope(envelope);
    
    Envelope deserialized;
    ASSERT_TRUE(deserialize_envelope(json, deserialized));
    
    EXPECT_EQ(envelope.topic, deserialized.topic);
    EXPECT_EQ(envelope.correlation_id, deserialized.correlation_id);
    EXPECT_EQ(envelope.payload_json, deserialized.payload_json);
    EXPECT_EQ(envelope.ts_ms, deserialized.ts_ms);
}

TEST(EnvelopeSerialization, SpecialCharacters) {
    Envelope envelope;
    envelope.topic = "test.topic";
    envelope.correlation_id = "test-id";
    envelope.payload_json = R"({"message":"Hello \"World\"","newline":"line1\nline2"})";
    envelope.ts_ms = 0;
    
    std::string json = serialize_envelope(envelope);
    
    Envelope deserialized;
    ASSERT_TRUE(deserialize_envelope(json, deserialized));
    
    EXPECT_EQ(envelope.topic, deserialized.topic);
    EXPECT_EQ(envelope.correlation_id, deserialized.correlation_id);
    EXPECT_EQ(envelope.payload_json, deserialized.payload_json);
}

TEST(EnvelopeSerialization, InvalidJson) {
    Envelope envelope;
    std::string invalid_json = "not valid json";
    
    EXPECT_FALSE(deserialize_envelope(invalid_json, envelope));
}

TEST(EnvelopeSerialization, EmptyEnvelope) {
    Envelope original;
    original.topic = "";
    original.correlation_id = "";
    original.payload_json = "{}";
    original.ts_ms = 0;
    
    std::string json = serialize_envelope(original);
    
    Envelope deserialized;
    ASSERT_TRUE(deserialize_envelope(json, deserialized));
    
    EXPECT_EQ(original.topic, deserialized.topic);
    EXPECT_EQ(original.correlation_id, deserialized.correlation_id);
    EXPECT_EQ(original.payload_json, deserialized.payload_json);
    EXPECT_EQ(original.ts_ms, deserialized.ts_ms);
}
