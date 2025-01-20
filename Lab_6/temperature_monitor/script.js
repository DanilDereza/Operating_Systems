
document.addEventListener('DOMContentLoaded', function() {
  const currentButton = document.getElementById('currentButton');
  const hourlyButton = document.getElementById('hourlyButton');
  const dailyButton = document.getElementById('dailyButton');
  const dataBody = document.getElementById('dataBody');
  const tableContainer = document.getElementById('tableContainer');
  const chartContainer = document.getElementById('chartContainer');
  const myChartCanvas = document.getElementById('myChart');
  let myChart;

  function clearTable() {
      dataBody.innerHTML = '';
  }

  function displayDataInTable(data) {
  clearTable();

  if(Array.isArray(data)) {
      if(data.length == 0) {
          const row = document.createElement('tr');
          const cell = document.createElement('td');
          cell.textContent = "No data available";
          cell.colSpan = 2;
          row.appendChild(cell);
          dataBody.appendChild(row);
          return;
      }
      data.forEach(item => {
          const row = document.createElement('tr');
          const timestampCell = document.createElement('td');
          const temperatureCell = document.createElement('td');
          timestampCell.textContent = item.timestamp;
          temperatureCell.textContent = item.temperature;
          row.appendChild(timestampCell);
          row.appendChild(temperatureCell);
          dataBody.appendChild(row);
          });
      } else {
          const row = document.createElement('tr');
          const timestampCell = document.createElement('td');
          const temperatureCell = document.createElement('td');
          timestampCell.textContent = get_current_time_for_display();
          temperatureCell.textContent = data.temperature;
          row.appendChild(timestampCell);
          row.appendChild(temperatureCell);
          dataBody.appendChild(row);
       }
  }

  function processHourlyData(data) {
     const groupedData = {};
     data.forEach(item => {
        const hour = item.timestamp.slice(0, 13); // YYYY-MM-DD HH
        if (!groupedData[hour]) {
          groupedData[hour] = { sum: 0, count: 0 };
       }
        groupedData[hour].sum += parseFloat(item.temperature);
       groupedData[hour].count += 1;
   });

   const result = Object.entries(groupedData).map(([hour, { sum, count }]) => ({
    timestamp: `${hour}:00`, // Добавляем минуты "00"
     temperature: sum / count,
     }));
    return result.slice(-30); // Возвращаем последние 30 записей
 }

function processDailyData(data) {
 const groupedData = {};
 data.forEach(item => {
     const day = item.timestamp.slice(0, 10); // YYYY-MM-DD
    if (!groupedData[day]) {
          groupedData[day] = { sum: 0, count: 0 };
       }
      groupedData[day].sum += parseFloat(item.temperature);
       groupedData[day].count += 1;
  });

  const result = Object.entries(groupedData).map(([day, { sum, count }]) => ({
      timestamp: day,
      temperature: sum / count,
    }));

    return result.slice(-366); // Возвращаем последние 366 записей
  }

  function fetchStatsData(endpoint) {
      const now = new Date();
      const startTime = new Date(now);
      
      if(endpoint === "/stats/daily") {
            startTime.setDate(now.getDate() - 365);
      } else if(endpoint === "/stats/hourly") {
            startTime.setMonth(now.getMonth() - 1);
      } else {
            startTime.setDate(now.getDate() - 1);
      }
      
      const formattedStartTime = startTime.toISOString().slice(0, 19).replace("T", " ");
      const formattedEndTime = now.toISOString().slice(0, 19).replace("T", " ");


      const url = `/stats?start=${formattedStartTime}&end=${formattedEndTime}`;

      fetch(url)
        .then(response => {
             if (!response.ok) {
                throw new Error(`HTTP error! Status: ${response.status}`);
               }
             return response.json();
         })
          .then(data => {
              let processedData = data;
              if (endpoint === "/current") {
                // Обрабатываем объект для /current как надо
                  processedData = data;
                } else if (endpoint === "/stats/hourly") {
                      processedData = processHourlyData(data);
                  } else if (endpoint === "/stats/daily") {
                     processedData = processDailyData(data);
                  }
              displayDataInTable(processedData);
             if (endpoint != "/current")
                  displayChart(processedData, endpoint);
             else{
              chartContainer.style.display = 'none';
                myChartCanvas.style.display = 'none';
             }

       })
       .catch(error => {
            console.error('Error fetching data:', error.message);
                const row = document.createElement('tr');
                 const cell = document.createElement('td');
                   cell.textContent = "Error fetching data from server.";
                   cell.colSpan = 2;
                   row.appendChild(cell);
                   dataBody.appendChild(row);
                   chartContainer.style.display = 'none';
                   myChartCanvas.style.display = 'none';

       });
}

function displayChart(data, endpoint) {
   if (data.length === 0) {
      chartContainer.style.display = 'none';
        myChartCanvas.style.display = 'none';
       return;
   }
   chartContainer.style.display = 'block';
    myChartCanvas.style.display = 'block';

   const labels = data.map(item => item.timestamp);
   const temps = data.map(item => parseFloat(item.temperature));

   if (myChart) {
       myChart.destroy();
   }

   myChart = new Chart(myChartCanvas, {
       type: 'line',
       data: {
           labels: labels,
           datasets: [{
               label: 'Temperature',
               data: temps,
               borderColor: 'rgb(75, 192, 192)',
               tension: 0.1
           }]
       },
         options: {
           responsive: true,
             maintainAspectRatio: false,
           scales: {
             x: {
                ticks: {
                     autoSkip: true,
                     maxTicksLimit: 10
                 }
             }
            }
       }
   });
}

function get_current_time_for_display() {
  const now = new Date();
  const year = now.getFullYear();
  const month = String(now.getMonth() + 1).padStart(2, '0');
  const day = String(now.getDate()).padStart(2, '0');
  const hours = String(now.getHours()).padStart(2, '0');
  const minutes = String(now.getMinutes()).padStart(2, '0');
  const seconds = String(now.getSeconds()).padStart(2, '0');
  const milliseconds = String(now.getMilliseconds()).padStart(3, '0');
  return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}.${milliseconds}`;
}

currentButton.addEventListener('click', function() {
   tableContainer.style.display = 'block';
     chartContainer.style.display = 'none';
   fetchStatsData("/current");
});

hourlyButton.addEventListener('click', function() {
   tableContainer.style.display = 'block';
  fetchStatsData("/stats/hourly");
});

dailyButton.addEventListener('click', function() {
     tableContainer.style.display = 'block';
    fetchStatsData("/stats/daily");
});

currentButton.click();
});
