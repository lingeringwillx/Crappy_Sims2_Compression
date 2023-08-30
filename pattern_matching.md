The following ways could be you used to find matching patterns within a file:

#### Hash table

1- A Map/Hash table where the keys are combinations of 3-bytes from the file converted to an integer and the values are a list of the locations where the pattern could be found.

This map is fast to build but consumes too much memory.
A 2-byte hash table is also possible, but you would have to check the third byte to confirm that you've got a match (An array could also be used).

2- A Map/Hash table where the keys are combinations of 3-bytes from the file converted to an integer and the value is the last location where the pattern could be found.

This is the lightest approach and the compression is still decent, although weaker than other compressions. The map would have to be updated as we loop through the file.

#### Hash chain

zlib uses a Hash chain to find patterns within the file, but the implementation details of the hash chain are unclear to me.

-----

**For all of the above methods** we loop over the bytes in the current location and the previous locations where the same pattern could be found to figure out the longest match.

Attempting to check all of the locations where there same pattern is found would result in the compression being too slow, so typically limitations are imposed on the searching loops to speed up the process. Two common limits are:

a- Limit the number of locations to check when there is too many locations where the same pattern could be found.

b- Finish searching and use the match that we have once a match above a certain length is found.

-----

#### Trie

Tries can also be used to find the longest match. However, a regular trie would use up all memory and take a long time to build. Hence, special optimizations would have to be implemented to make it work. It's also more difficult to implement.

#### Suffix Array

I found this difficult to use for the purpose of compression.
