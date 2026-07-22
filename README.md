[Powered by Andromeda Hack](https://andromeda.buzz/)

## Safe validation

Place the built executable and DLL in the same directory, then run:

```powershell
.\Andromeda-Dota2-Base.exe --dry-run
```

The dry run validates the payload PE, checks query-only access to the running Dota 2 process, and scans the on-disk `client.dll` for the expected local-player AOB. It does not request debug privileges, invoke the manual mapper, or modify target-process memory. No-argument execution does not inject; the legacy path now requires an explicit flag.

<img width="1600" height="900" alt="image" src="https://github.com/user-attachments/assets/9bc5fe8b-c788-49ed-8de0-ac43e026c0ab" />
<img width="1600" height="900" alt="image" src="https://github.com/user-attachments/assets/bf9506b6-dd6a-4617-9f37-ed4d7a55302d" />
