# Preserved development history

The default branch is the maintained source. The tags below preserve earlier source or separate candidates at exact commits.
No archived candidate gains new build, package, or gameplay acceptance through this cleanup.

A tag is a fixed source snapshot. Existing tree URLs and `git clone --branch <name>` can select these tags.
For local inspection, use `git fetch origin --tags` followed by `git switch --detach refs/tags/<name>`.
To resume development, create a temporary branch from the tag. Do not move an archive tag.

| Tag | Preserved commit | Disposition |
|---|---|---|
| `feature/sf1-mouse-look` | [`17669d4a2b21c0ca5e8a0f7c93d7088dadf938d1`](https://github.com/Alexbeav/SF-pc-port/tree/17669d4a2b21c0ca5e8a0f7c93d7088dadf938d1) | Separate historical source; no unrelated-history merge. |
| `feature/sf1-mouse-look-upstream` | [`c24ce313b1356da2e3d5615f3001f6000e399f99`](https://github.com/Alexbeav/SF-pc-port/tree/c24ce313b1356da2e3d5615f3001f6000e399f99) | Separate historical source; no unrelated-history merge. |
| `phase1/sf1-standardized-baseline-20260813` | [`6fae2a710c2448cc4dc527ec9de54abc53ed2b1b`](https://github.com/Alexbeav/SF-pc-port/tree/6fae2a710c2448cc4dc527ec9de54abc53ed2b1b) | Separate historical source; no unrelated-history merge. |
| `feature/sf2-guest-runtime` | [`a1043f27778e0f0d345cdd44b7743619a1ae0b66`](https://github.com/Alexbeav/SF-pc-port/tree/a1043f27778e0f0d345cdd44b7743619a1ae0b66) | Earlier title implementation or enhancement; no product promotion. |
| `feature/sf2-modern-presentation` | [`411005f118e564b386bb903bf6aec5f17a17a44c`](https://github.com/Alexbeav/SF-pc-port/tree/411005f118e564b386bb903bf6aec5f17a17a44c) | Earlier title implementation or enhancement; no product promotion. |
| `research/sf2-full-bringup` | [`4476381c0d9ae6c40161e2f2e109717fb28bfee0`](https://github.com/Alexbeav/SF-pc-port/tree/4476381c0d9ae6c40161e2f2e109717fb28bfee0) | Earlier title implementation or enhancement; no product promotion. |
| `research/sf3-full-bringup` | [`3af7806cbba66ab971aa11c0e40ec4e58b71bfa8`](https://github.com/Alexbeav/SF-pc-port/tree/3af7806cbba66ab971aa11c0e40ec4e58b71bfa8) | Earlier title implementation or enhancement; no product promotion. |

Recorded 2026-09-13. Existing version tags and releases remain unchanged.
