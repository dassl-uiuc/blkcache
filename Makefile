all:
	g++ -o reader reader.cc -lstdc++
	g++ -o writer writer.cc -lstdc++
clean:
	rm -rf reader
	rm -rf writer