document.addEventListener("DOMContentLoaded", function () {
  var estado = document.getElementById("js-estado");
  estado.textContent = "JavaScript cargado correctamente";
  estado.className = "ok";

  var contador = 0;
  document.getElementById("btn").addEventListener("click", function () {
    contador++;
    document.getElementById("salida").textContent =
      "Clic numero " + contador + " - " + new Date().toLocaleTimeString();
  });
});
