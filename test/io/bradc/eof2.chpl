var infile = open("eof.in", mode.r).reader();

while !infile.eof {
  var s = infile.read(string);
  writeln(s);
}