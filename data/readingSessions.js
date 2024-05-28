document.addEventListener("DOMContentLoaded", function () {
    fetch("chargeSessions.txt")
        .then(response => response.text())
        .then(data => {
            const csvTable = document.getElementById("csvTable");
            const rows = data.split("\n");
            
            rows.forEach(row => {
                const cells = row.split(",");
                const tableRow = document.createElement("tr");

                cells.forEach(cell => {
                    const tableCell = document.createElement("td");
                    tableCell.textContent = cell;
                    tableRow.appendChild(tableCell);
                });

                csvTable.appendChild(tableRow);
            });
        })
        .catch(error => console.error("Fehler beim Abrufen der CSV-Datei", error));
});