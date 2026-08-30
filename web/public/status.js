// Progressive enhancement: without script the pill just says it is checking.
function show(online, players) {
  document.getElementById('status-dot').className = 'dot ' + (online ? 'up' : 'down');
  var note = 'Offline';
  if (online) {
    note = typeof players === 'number'
        ? players + (players === 1 ? ' Trainer Online' : ' Trainers Online')
        : 'Online';
  }
  document.getElementById('status-note').textContent = note;
}

fetch('/api/status', { headers: { accept: 'application/json' } })
  .then(function (response) { return response.json(); })
  .then(function (status) { show(status.login && status.game, status.players); })
  .catch(function () { show(false); });
