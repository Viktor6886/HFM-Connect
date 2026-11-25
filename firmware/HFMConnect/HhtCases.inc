case 0x0000:
  if (!bosch) {
    if (diag <= 72) return eval(0,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(0,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(0,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(0,r);
    if (diag >= 74) return eval(0,r);
  }
  else {
    if (diag <= 72) return eval(1,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(1,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(1,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(1,r);
    if (diag >= 74) return eval(1,r);
  }
  break;
case 0x0120:
  if (!bosch) {
    if (diag <= 68) return eval(2,r);
    if (diag >= 69 && diag <= 72) return eval(3,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(4,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(4,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(4,r);
    if (diag >= 74) return eval(4,r);
  }
  else {
    if (diag <= 68) return eval(5,r);
    if (diag >= 69 && diag <= 72) return eval(6,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(6,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(6,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(6,r);
    if (diag >= 74) return eval(6,r);
  }
  break;
case 0x0292:
  if (!bosch) {
    if (diag <= 68) return eval(7,r);
    if (diag >= 69 && diag <= 72) return eval(8,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(8,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(8,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(8,r);
    if (diag >= 74) return eval(8,r);
  }
  else {
    if (diag <= 72) return eval(9,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(9,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(9,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(9,r);
    if (diag >= 74) return eval(9,r);
  }
  break;
case 0x0361:
  if (!bosch) {
    if (diag <= 72) return eval(10,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(10,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(10,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(10,r);
    if (diag >= 74) return eval(10,r);
  }
  else {
    if (diag <= 72) return eval(11,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(11,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(11,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(11,r);
    if (diag >= 74) return eval(11,r);
  }
  break;
case 0x03c8:
  if (!bosch) {
    if (diag <= 68) return eval(12,r);
    if (diag >= 69 && diag <= 72) return eval(13,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(13,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(13,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(13,r);
    if (diag >= 74) return eval(13,r);
  }
  else {
    if (diag <= 72) return eval(14,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(14,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(14,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(14,r);
    if (diag >= 74) return eval(14,r);
  }
  break;
case 0x042f:
  if (!bosch) {
    if (diag <= 72) return eval(15,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(15,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(15,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(15,r);
    if (diag >= 74) return eval(15,r);
  }
  else {
    if (diag <= 68) return eval(15,r);
    if (diag >= 69 && diag <= 72) return eval(16,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(16,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(16,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(16,r);
    if (diag >= 74) return eval(16,r);
  }
  break;
case 0x04df:
  if (!bosch) {
    if (diag <= 68) return eval(17,r);
    if (diag >= 69 && diag <= 69) return eval(18,r);
    if (diag >= 70 && diag <= 72) return eval(19,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(19,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(19,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(19,r);
    if (diag >= 74) return eval(19,r);
  }
  else {
    if (diag <= 68) return eval(20,r);
    if (diag >= 69 && diag <= 72) return eval(21,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(21,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(21,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(21,r);
    if (diag >= 74) return eval(21,r);
  }
  break;
case 0x0633:
  if (!bosch) {
    if (diag <= 65) return eval(22,r);
    if (diag >= 66 && diag <= 68) return eval(23,r);
    if (diag >= 69 && diag <= 72) return eval(13,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(13,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(13,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(13,r);
    if (diag >= 74) return eval(13,r);
  }
  else {
    if (diag <= 72) return eval(24,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(24,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(24,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(24,r);
    if (diag >= 74) return eval(24,r);
  }
  break;
case 0x0777:
  if (!bosch) {
    if (diag <= 68) return eval(25,r);
    if (diag >= 69 && diag <= 72) return eval(26,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(26,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(26,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(26,r);
    if (diag >= 74) return eval(26,r);
  }
  else {
    if (diag <= 72) return eval(27,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(27,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(27,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(27,r);
    if (diag >= 74) return eval(27,r);
  }
  break;
case 0x085e:
  if (!bosch) {
    if (diag <= 68) return eval(25,r);
    if (diag >= 69 && diag <= 72) return eval(26,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(26,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(26,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(26,r);
    if (diag >= 74) return eval(26,r);
  }
  else {
    if (diag <= 72) return eval(27,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(27,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(27,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(27,r);
    if (diag >= 74) return eval(27,r);
  }
  break;
case 0x0a87:
  if (!bosch) {
    if (diag <= 72) return eval(0,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(0,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(0,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(0,r);
    if (diag >= 74) return eval(0,r);
  }
  else {
    if (diag <= 72) return eval(28,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(28,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(28,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(28,r);
    if (diag >= 74) return eval(28,r);
  }
  break;
case 0x0ac9:
  if (!bosch) {
    return oxygenRange(r,diag,revision);
  }
  else {
    if (diag <= 72) return eval(29,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(29,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(29,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(29,r);
    if (diag >= 74) return eval(29,r);
  }
  break;
case 0x0e46:
  if (!bosch) {
    if (diag <= 68) return eval(30,r);
    if (diag >= 69 && diag <= 69) return eval(31,r);
    if (diag >= 70 && diag <= 72) return eval(32,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(32,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(31,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(32,r);
    if (diag >= 74) return eval(32,r);
  }
  else {
    if (diag <= 72) return eval(33,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(33,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(33,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(33,r);
    if (diag >= 74) return eval(33,r);
  }
  break;
case 0x11fe:
  if (!bosch) {
    if (diag <= 72) return eval(34,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(34,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(34,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(34,r);
    if (diag >= 74) return eval(34,r);
  }
  else {
    if (diag <= 72) return eval(34,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(34,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(34,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(34,r);
    if (diag >= 74) return eval(34,r);
  }
  break;
case 0x1224:
  if (!bosch) {
    if (diag <= 69) return eval(35,r);
    if (diag >= 70 && diag <= 72) return eval(36,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(36,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(35,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(36,r);
    if (diag >= 74) return eval(36,r);
  }
  else {
    if (diag <= 72) return eval(37,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(37,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(37,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(37,r);
    if (diag >= 74) return eval(37,r);
  }
  break;
case 0x1302:
  if (!bosch) {
    if (diag <= 72) return eval(38,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(38,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(38,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(38,r);
    if (diag >= 74) return eval(38,r);
  }
  else {
    if (diag <= 72) return eval(39,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(39,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(39,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(39,r);
    if (diag >= 74) return eval(39,r);
  }
  break;
case 0x135f:
  if (!bosch) {
    if (diag <= 72) return eval(40,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(40,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(40,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(40,r);
    if (diag >= 74) return eval(40,r);
  }
  else {
    if (diag <= 72) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(41,r);
    if (diag >= 74) return eval(41,r);
  }
  break;
case 0x139e:
  if (!bosch) {
    if (diag <= 72) return eval(42,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(42,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(42,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(42,r);
    if (diag >= 74) return eval(42,r);
  }
  else {
    if (diag <= 72) return eval(43,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(43,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(43,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(43,r);
    if (diag >= 74) return eval(43,r);
  }
  break;
case 0x140e:
  if (!bosch) {
    if (diag <= 68) return eval(44,r);
    if (diag >= 69 && diag <= 69) return eval(45,r);
    if (diag >= 70 && diag <= 72) return eval(46,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(46,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(45,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(46,r);
    if (diag >= 74) return eval(46,r);
  }
  else {
    if (diag <= 72) return eval(33,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(33,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(33,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(33,r);
    if (diag >= 74) return eval(33,r);
  }
  break;
case 0x1ba3:
  if (!bosch) {
    if (diag <= 69) return eval(47,r);
    if (diag >= 70 && diag <= 72) return eval(42,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(42,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(42,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(42,r);
    if (diag >= 74) return eval(42,r);
  }
  else {
    if (diag <= 72) return eval(43,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(43,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(43,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(43,r);
    if (diag >= 74) return eval(43,r);
  }
  break;
case 0x1c6b:
  if (!bosch) {
    if (diag <= 72) return eval(48,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(48,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(48,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(48,r);
    if (diag >= 74) return eval(48,r);
  }
  else {
    if (diag <= 72) return eval(40,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(40,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(40,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(40,r);
    if (diag >= 74) return eval(40,r);
  }
  break;
case 0x1cb2:
  if (!bosch) {
    if (diag <= 72) return eval(49,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(49,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(49,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(49,r);
    if (diag >= 74) return eval(49,r);
  }
  else {
    if (diag <= 72) return eval(49,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(49,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(49,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(49,r);
    if (diag >= 74) return eval(49,r);
  }
  break;
case 0x1cce:
  if (!bosch) {
    if (diag <= 72) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(50,r);
    if (diag >= 74 && diag <= 79) return eval(50,r);
    if (diag >= 80) return eval(51,r);
  }
  else {
    if (diag <= 72) return eval(52,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(52,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(52,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(52,r);
    if (diag >= 74) return eval(52,r);
  }
  break;
case 0x1d7d:
  if (!bosch) {
    if (diag <= 72) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(50,r);
    if (diag >= 74) return eval(50,r);
  }
  else {
    if (diag <= 72) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(50,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(50,r);
    if (diag >= 74) return eval(50,r);
  }
  break;
case 0x1da2:
  if (!bosch) {
    if (diag <= 72) return eval(53,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(53,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(53,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(53,r);
    if (diag >= 74) return eval(53,r);
  }
  else {
    if (diag <= 72) return eval(54,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(54,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(54,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(54,r);
    if (diag >= 74) return eval(54,r);
  }
  break;
case 0x1e36:
  if (!bosch) {
    if (diag <= 72) return eval(55,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(55,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(55,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(55,r);
    if (diag >= 74) return eval(55,r);
  }
  else {
    if (diag <= 72) return eval(55,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(55,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(55,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(55,r);
    if (diag >= 74) return eval(55,r);
  }
  break;
case 0x1e72:
  if (!bosch) {
    if (diag <= 72) return eval(56,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(56,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(56,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(56,r);
    if (diag >= 74 && diag <= 79) return eval(56,r);
    if (diag >= 80) return eval(57,r);
  }
  else {
    if (diag <= 72) return eval(58,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(58,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(58,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(58,r);
    if (diag >= 74) return eval(58,r);
  }
  break;
case 0x2066:
  if (!bosch) {
    if (diag <= 72) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(41,r);
    if (diag >= 74) return eval(41,r);
  }
  else {
    if (diag <= 72) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(41,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(41,r);
    if (diag >= 74) return eval(41,r);
  }
  break;
case 0x2088:
  if (!bosch) {
    if (diag <= 72) return eval(59,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(59,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(59,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(59,r);
    if (diag >= 74) return eval(59,r);
  }
  else {
    if (diag <= 72) return eval(60,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(60,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(60,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(60,r);
    if (diag >= 74) return eval(60,r);
  }
  break;
case 0x2118:
  if (!bosch) {
    if (diag <= 72) return eval(48,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(48,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(48,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(48,r);
    if (diag >= 74) return eval(48,r);
  }
  else {
    if (diag <= 72) return eval(61,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(61,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(61,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(61,r);
    if (diag >= 74) return eval(61,r);
  }
  break;
case 0x2171:
  if (!bosch) {
    if (diag <= 72) return eval(62,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(62,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(62,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(62,r);
    if (diag >= 74) return eval(62,r);
  }
  else {
    if (diag <= 72) return eval(62,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(62,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(62,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(62,r);
    if (diag >= 74) return eval(62,r);
  }
  break;
case 0x21e7:
  if (!bosch) {
    if (diag <= 72) return eval(63,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(63,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(63,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(63,r);
    if (diag >= 74) return eval(63,r);
  }
  else {
    if (diag <= 72) return eval(63,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(63,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(63,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(63,r);
    if (diag >= 74) return eval(63,r);
  }
  break;
case 0x2241:
  if (!bosch) {
    if (diag <= 72) return eval(64,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(64,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(64,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(64,r);
    if (diag >= 74) return eval(64,r);
  }
  else {
    if (diag <= 72) return eval(64,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(64,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(64,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(64,r);
    if (diag >= 74) return eval(64,r);
  }
  break;
case 0x22b7:
  if (!bosch) {
    if (diag <= 72) return eval(65,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(65,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(65,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(65,r);
    if (diag >= 74) return eval(65,r);
  }
  else {
    if (diag <= 72) return eval(65,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(65,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(65,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(65,r);
    if (diag >= 74) return eval(65,r);
  }
  break;
case 0x2381:
  if (!bosch) {
    if (diag <= 72) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(66,r);
    if (diag >= 74) return eval(66,r);
  }
  else {
    if (diag <= 72) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(66,r);
    if (diag >= 74) return eval(66,r);
  }
  break;
case 0x23b0:
  if (!bosch) {
    if (diag <= 72) return eval(67,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(67,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(67,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(67,r);
    if (diag >= 74) return eval(67,r);
  }
  else {
    if (diag <= 72) return eval(67,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(67,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(67,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(67,r);
    if (diag >= 74) return eval(67,r);
  }
  break;
case 0x23e6:
  if (!bosch) {
    if (diag <= 72) return eval(68,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(68,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(68,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(68,r);
    if (diag >= 74) return eval(68,r);
  }
  else {
    if (diag <= 72) return eval(68,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(68,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(68,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(68,r);
    if (diag >= 74) return eval(68,r);
  }
  break;
case 0x240b:
  if (!bosch) {
    if (diag <= 72) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(66,r);
    if (diag >= 74) return eval(66,r);
  }
  else {
    if (diag <= 72) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(66,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(66,r);
    if (diag >= 74) return eval(66,r);
  }
  break;
case 0x243a:
  if (!bosch) {
    if (diag <= 72) return eval(69,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(69,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(69,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(69,r);
    if (diag >= 74) return eval(69,r);
  }
  else {
    if (diag <= 72) return eval(69,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(69,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(69,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(69,r);
    if (diag >= 74) return eval(69,r);
  }
  break;
case 0x245f:
  if (!bosch) {
    if (diag <= 72) return eval(70,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(70,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(70,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(70,r);
    if (diag >= 74) return eval(70,r);
  }
  else {
    if (diag <= 72) return eval(70,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(70,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(70,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(70,r);
    if (diag >= 74) return eval(70,r);
  }
  break;
case 0x247e:
  if (!bosch) {
    if (diag <= 72) return eval(71,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(71,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(71,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(71,r);
    if (diag >= 74) return eval(71,r);
  }
  else {
    if (diag <= 72) return eval(71,r);
    if (diag >= 73 && diag <= 73 && revision < 0x50) return eval(71,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x50 && revision < 0x60) return eval(71,r);
    if (diag >= 73 && diag <= 73 && revision >= 0x60) return eval(71,r);
    if (diag >= 74) return eval(71,r);
  }
  break;
