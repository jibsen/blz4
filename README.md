
blz4
====

About
-----

This is an example using some of the compression algorithms from [BriefLZ][]
to produce output in the format of [LZ4][].

**Please note:** this is just a quick experiment to see how it would work, it
is not production quality, and has not been properly tested.

[BriefLZ]: https://github.com/jibsen/brieflz
[LZ4]: https://github.com/lz4/lz4


Benchmark
---------

Here are some results on the [Silesia compression corpus][silesia]:

| File    |   Original | `blz4 --optimal` | `lz4 -12 -l` |  `lz4x -9` |
| :------ | ---------: | ---------------: | -----------: | ---------: |
| dickens | 10.192.446 |        4.380.430 |    4.380.430 |  4.380.430 |
| mozilla | 51.220.480 |       22.025.940 |   22.025.988 | 22.025.940 |
| mr      |  9.970.564 |        4.190.675 |    4.190.774 |  4.190.675 |
| nci     | 33.553.445 |        3.621.482 |    3.621.567 |  3.621.482 |
| ooffice |  6.152.192 |        3.535.258 |    3.535.258 |  3.535.258 |
| osdb    | 10.085.684 |        3.951.474 |    3.951.474 |  3.951.474 |
| reymont |  6.627.202 |        2.063.060 |    2.063.060 |  2.063.060 |
| samba   | 21.606.400 |        6.100.521 |    6.100.539 |  6.100.521 |
| sao     |  7.251.944 |        5.668.742 |    5.668.742 |  5.668.742 |
| webster | 41.458.703 |       13.835.336 |   13.835.336 | 13.835.336 |
| xml     |  5.345.280 |          759.868 |      759.901 |    759.868 |
| x-ray   |  8.474.240 |        7.177.203 |    7.177.203 |  7.177.203 |

I did not include smallz4 because it does not create output in the legacy
format, so the results are not directly comparable on files larger than
4MiB.

[silesia]: http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia


Usage
-----

blz4 uses [Meson][] to generate build systems. To create one for the tools on
your platform, and build blz4, use something along the lines of:

~~~sh
mkdir build
cd build
meson ..
ninja
~~~

You can also simply compile and link the source files.

blz4 includes the leparse and ssparse algorithms from BriefLZ, which gives
compression levels `-5` to `-9` and the **very** slow `--optimal`.

[Meson]: https://mesonbuild.com/


Notes
-----

  - blz4 is finding the closest match of each length, but since match offsets
    are coded using two bytes regardless of distance, it is enough to find the
    longest match at each position. If we replaced the match finding with a
    suffix array or -tree, that should speed up ssparse.
  - LZ4 appears to do flexible parsing, is very close to optimal, and much
    faster.


Related Projects
----------------

  - [LZ4X](https://github.com/encode84/lz4x)
  - [smallz4](https://create.stephan-brumme.com/smallz4/)


License
-------

This projected is licensed under the [zlib License](LICENSE) (Zlib).
