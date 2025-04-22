window.BENCHMARK_DATA = {
  "lastUpdate": 1745335875885,
  "repoUrl": "https://github.com/tdrwenski/quandary",
  "entries": {
    "Benchmark": [
      {
        "commit": {
          "author": {
            "name": "tdrwenski",
            "username": "tdrwenski"
          },
          "committer": {
            "name": "tdrwenski",
            "username": "tdrwenski"
          },
          "id": "eec732819c89289d4b92b0e9dbf1adc0752fc197",
          "message": "Perf testing",
          "timestamp": "2025-04-10T21:41:33Z",
          "url": "https://github.com/tdrwenski/quandary/pull/9/commits/eec732819c89289d4b92b0e9dbf1adc0752fc197"
        },
        "date": 1745273456247,
        "tool": "pytest",
        "benches": [
          {
            "name": "performance_tests/performance_test.py::test_eval[config_template_1]",
            "value": 0.3814313215949884,
            "unit": "iter/sec",
            "range": "stddev: 0.04423256110923546",
            "extra": "mean: 2.6217039435000062 sec\nrounds: 10"
          },
          {
            "name": "performance_tests/performance_test.py::test_eval[config_template_4]",
            "value": 0.7254830020196964,
            "unit": "iter/sec",
            "range": "stddev: 0.02055754621866016",
            "extra": "mean: 1.3783920467000144 sec\nrounds: 10"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "name": "tdrwenski",
            "username": "tdrwenski"
          },
          "committer": {
            "name": "tdrwenski",
            "username": "tdrwenski"
          },
          "id": "eec732819c89289d4b92b0e9dbf1adc0752fc197",
          "message": "Perf testing",
          "timestamp": "2025-04-10T21:41:33Z",
          "url": "https://github.com/tdrwenski/quandary/pull/9/commits/eec732819c89289d4b92b0e9dbf1adc0752fc197"
        },
        "date": 1745335875100,
        "tool": "pytest",
        "benches": [
          {
            "name": "performance_tests/performance_test.py::test_eval[config_template_1]",
            "value": 0.3829993955456479,
            "unit": "iter/sec",
            "range": "stddev: 0.02031925432070175",
            "extra": "mean: 2.6109701780999672 sec\nrounds: 10"
          },
          {
            "name": "performance_tests/performance_test.py::test_eval[config_template_4]",
            "value": 0.7245771131809527,
            "unit": "iter/sec",
            "range": "stddev: 0.013803514249836968",
            "extra": "mean: 1.380115355300029 sec\nrounds: 10"
          }
        ]
      }
    ]
  }
}