# keys

Generates the key pairs the servers sign with and the client verifies against.

You do not normally run this. Every module that needs a key depends on it and
it runs on its own. To do it by hand:

```bash
./gradlew :keys:generateGame
```

Keys land in `build/` and are gitignored. Never commit one.

Two pairs exist: `game`, used by the login and game servers, and `chat`.
Both are ECDSA on secp256r1, written as PEM.
