# Benchmark Comparison

Benchmarks compare 1term against Alacritty using the [vtebench](https://github.com/alacritty/vtebench) suite.

| **Test**                             | **Alacritty (ms)**                              | **1term (ms)**                                 | **Difference**            |
| ------------------------------------ | ----------------------------------------------- | ----------------------------------------------- | ------------------------- |
| **cursor_motion**                    | 5.79ms avg (90% < 6ms) ± 0.66ms                | 14.5ms avg (90% < 54ms) ± 21.64ms              | 8.71ms slower             |
| **dense_cells**                       | 8.53ms avg (90% < 9ms) ± 0.77ms                | 22.28ms avg (90% < 61ms) ± 23.68ms             | 13.75ms slower            |
| **light_cells**                       | 6.53ms avg (90% < 7ms) ± 0.67ms                | 3.48ms avg (90% < 8ms) ± 2.71ms                | 3.05ms faster             |
| **medium_cells**                      | 9.85ms avg (90% < 11ms) ± 0.92ms               | 9ms avg (90% < 34ms) ± 13.42ms                 | 0.85ms faster             |
| **scrolling**                         | 123.67ms avg (90% < 136ms) ± 8.13ms            | 165.65ms avg (90% < 177ms) ± 7.57ms            | 41.98ms slower            |
| **scrolling_bottom_region**           | 125.66ms avg (90% < 139ms) ± 9.93ms            | 155.77ms avg (90% < 166ms) ± 9.12ms            | 30.11ms slower            |
| **scrolling_bottom_small_region**     | 123.32ms avg (90% < 136ms) ± 11.05ms           | 159.98ms avg (90% < 173ms) ± 11.73ms           | 36.66ms slower            |
| **scrolling_fullscreen**              | 8.87ms avg (90% < 11ms) ± 1.22ms               | 9.1ms avg (90% < 18ms) ± 5.53ms                | 0.23ms slower             |
| **scrolling_top_region**              | 123.75ms avg (90% < 138ms) ± 11.14ms           | 167.85ms avg (90% < 182ms) ± 10.5ms            | 44.1ms slower             |
| **scrolling_top_small_region**        | 125.04ms avg (90% < 140ms) ± 11.49ms           | 160.11ms avg (90% < 179ms) ± 11.47ms           | 35.07ms slower            |
| **sync_medium_cells**                 | 10.46ms avg (90% < 11ms) ± 0.87ms              | 9.56ms avg (90% < 36ms) ± 14.31ms              | 0.9ms faster              |
| **unicode**                           | 6.76ms avg (90% < 7ms) ± 0.62ms                | 8ms avg (90% < 35ms) ± 16.28ms                 | 1.24ms slower             |

> **Note:** All times represent the **average latency** over several test samples, with the 90th percentile value and standard deviation included.

## Methodology

1. Both terminals were run on the same hardware under identical conditions.
2. The `vtebench` tool was used to generate terminal output and measure rendering latency.
3. Each test was repeated multiple times to ensure statistical significance.
4. The system was otherwise idle to minimize interference.

## Interpretation

- 1term performs competitively in cell‑heavy rendering (`light_cells`, `medium_cells`).
- Scrolling performance is slower, likely due to GTK4/VTE overhead.
- The variance (standard deviation) is higher for 1term, indicating less consistent rendering times.

## Future Optimizations

- Investigate GTK4 drawing optimizations.
- Evaluate VTE configuration options.
- Consider custom drawing for simple cell updates.