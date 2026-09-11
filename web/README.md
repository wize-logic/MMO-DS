# Website

The public face of a server: what it is, where to get the client, and the only
way to get an account, because the login protocol has no registration packet.

Two halves.

- `public/` is the whole site. Static HTML and one stylesheet, no build step.
  Apache serves it.
- `../server.web` is a small Kotlin service on loopback. It answers `/api/` and
  nothing else: the registration form posts to it, and the front page asks it
  whether the servers are up. It writes the same row `server.login create-user`
  writes, so an account made here works in the client.

## Deploying

```bash
sudo ./web/deploy.sh
```

Builds `server.web`, installs it under `/opt/openmmo/web` behind a systemd
unit, copies the site to `/var/www/openmmo`, installs the virtual host and
reloads apache. Run it again after any change. Pass `--no-build` to install
what is already built.

Settings land in `/etc/openmmo/web.env`, taken from the repository `.env` the
first time and never overwritten after that. It needs the `LOGIN_DB_*` values,
which is where accounts live.

```bash
sudo systemctl status openmmo-web
sudo journalctl -u openmmo-web -f
```

## Developing

```bash
./gradlew :server.web:run
```

That serves `public/` itself on <http://127.0.0.1:8088>, so the whole site
works with no apache and no deployment. `OPENMMO_WEB_ROOT` is what turns that
on; in production it is unset and the service serves no files.

## Behind Cloudflare

TLS is Cloudflare's. The origin speaks plain HTTP on port 80, so the DNS record
has to be proxied and **Always Use HTTPS** turned on, or visitors reach the site
unencrypted.

For Full (strict), put a Cloudflare origin certificate at
`/etc/ssl/openmmo/origin.pem` with its key beside it as `origin.key`, then
`sudo a2enmod ssl && sudo systemctl reload apache2`. The TLS virtual host in
`apache/openmmo.conf` switches itself on once both files exist.

Restrict port 80 to Cloudflare at the firewall too, or anyone who learns the
origin address can skip Cloudflare entirely:

```bash
for range in $(curl -s https://www.cloudflare.com/ips-v4) \
             $(curl -s https://www.cloudflare.com/ips-v6); do
  sudo ufw allow from "$range" to any port 80 proto tcp
done
```

Every request arrives from Cloudflare, so without
`apache/cloudflare-remoteip.conf` every visitor looks like the same few
addresses, both in the logs and to the per address registration limit.
`./web/update-cloudflare-ips.sh` regenerates that file when the ranges change.

## The domain

`apache/openmmo.conf` names `openmmo.dev`. Change `ServerName` and
`ServerAlias` for another domain and redeploy. It is the first enabled virtual
host, so it also answers for any name that matches nothing else, which is what
makes a bare address request work.

## What registration enforces

- 3 to 32 characters, letters digits underscore hyphen, stored lowercase.
- 8 to 128 character password, typed twice, and it may not contain the account
  name.
- Five accounts an hour per address, twenty requests an hour per address
  whether or not they were accounts, and 120 accounts an hour across everybody.

The client hashes the password before sending it, so a SHA-1 is what arrives.
The database does not keep that: the login server stores PBKDF2 over it with a
salt per row, so reading the table gives nothing that can be replayed as a
login.
