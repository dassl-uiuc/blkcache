all:
	g++ -o reader reader.cc -lstdc++
	g++ -o writer writer.cc -lstdc++
	g++ -o varied_size_accesses varied_size_accesses.cc -lstdc++
varied_size:
	g++ -o varied_size_accesses varied_size_accesses.cc -lstdc++
clean:
	rm -rf reader
	rm -rf varied_size_accesses
	rm -rf writer
