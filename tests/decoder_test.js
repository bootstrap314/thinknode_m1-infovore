const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const path = require('path');
const ctx = {};
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(__dirname, '../chirpstack/decoder.js'), 'utf8'), ctx);
const gps = ctx.decodeUplink({fPort: 1, bytes: [0,0,0,0,0,0,0,0,255,246,8,0,150]});
assert.equal(gps.data.latitude, 0);
assert.equal(gps.data.altitude_m, -10);
assert.equal(gps.data.hdop, 1.5);
assert.equal(ctx.decodeUplink({fPort: 10, bytes: [0]}).data.type, 'phone_poll');
assert.equal(ctx.decodeUplink({fPort: 20, bytes: [1,255,2]}).data.payload_hex, '01ff02');
for (const input of [
  {fPort: 1, bytes: [0]}, {fPort: 0, bytes: [0]}, {fPort: 224, bytes: [0]},
  {fPort: 2, bytes: []}, {fPort: 2, bytes: Array(33).fill(0)}
]) assert(ctx.decodeUplink(input).errors);
console.log('PASS: ChirpStack GPS and phone payload decoder');
