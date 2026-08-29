// Progressive enhancement: without script the pill just says it is checking.
function show(online) {
  document.getElementById('status-dot').className = 'dot ' + (online ? 'up' : 'down');
  document.getElementById('status-note').textContent = online ? 'Online' : 'Offline';
}

fetch('/api/status', { headers: { accept: 'application/json' } })
  .then(function (response) { return response.json(); })
  .then(function (status) { show(status.login && status.game); })
  .catch(function () { show(false); });
