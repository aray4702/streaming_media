## parity_report.py failed

```
Traceback (most recent call last):
  File "/home/runner/work/streaming_media/streaming_media/tools/parity_report.py", line 221, in <module>
    rc = main()
         ^^^^^^
  File "/home/runner/work/streaming_media/streaming_media/tools/parity_report.py", line 193, in main
    data = load(a.compare_json)
           ^^^^^^^^^^^^^^^^^^^^
  File "/home/runner/work/streaming_media/streaming_media/tools/parity_report.py", line 39, in load
    with open(path) as f:
         ^^^^^^^^^^
FileNotFoundError: [Errno 2] No such file or directory: 'compare-webrtc.json'

```

Values above 1e12:

```
could not scan the input: FileNotFoundError(2, 'No such file or directory')
```
