// ChirpStack v4 decoder for this custom M1 firmware, NOT the SenseCAP decoder.
function decodeUplink(input) {
  var b = input.bytes;
  if (input.fPort >= 2 && input.fPort <= 223 && b && b.length >= 1 && b.length <= 32) {
    var hex = b.map(function (v) { return ("0" + v.toString(16)).slice(-2); }).join("");
    return { data: {
      type: input.fPort === 10 && b.length === 1 && b[0] === 0 ? "phone_poll" : "phone_command",
      port: input.fPort,
      payload_hex: hex
    }};
  }
  if (input.fPort !== 1 || !b || b.length !== 13) {
    return { errors: ["Expected GPS on port 1 or a 1..32-byte phone payload on port 2..223"] };
  }
  function i32(i) {
    return (b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3];
  }
  var altitude = (b[8] << 8) | b[9];
  if (altitude & 0x8000) altitude -= 65536;
  var latitude = i32(0) / 1000000;
  var longitude = i32(4) / 1000000;
  if (latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
    return { errors: ["Invalid coordinates"] };
  }
  return { data: {
    latitude: latitude,
    longitude: longitude,
    altitude_m: altitude,
    satellites: b[10],
    hdop: ((b[11] << 8) | b[12]) / 100
  }};
}
