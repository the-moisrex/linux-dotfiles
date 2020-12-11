const express = require('express');
const process = require('process');
const fs = require('fs');
const app = express();
const port = 3000;
const pidFile = '.pid';

try {
  fs.access(pidFile, fs.constants.R_OK);
  const oldPID = fs.readFileSync(pidFile);
  process.kill(oldPID, 'SIGHUP');
} catch (err) {
  // nothing to do
}


app.use(express.static('./public'));

app.listen(port, () => {
  fs.writeFileSync(pidFile, `${process.pid}`);
  console.log(`Application is running at http://localhost:${port}\nProccess ID: ${process.pid}`);
});
