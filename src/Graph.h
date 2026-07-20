#include <Arduino.h>
#include <State.h>

#include <functional>

#define UNDEFINED_IDS -1

class Graph {
 public:
  Graph(int numGraphs, int numPoints, Print& logger)
      : _num_graphs(numGraphs), _num_points(numPoints), log(logger) {
    graphLength = min(graphLengthMax, numPoints);
    _ids = new int[numGraphs];
    for (int i = 0; i < _num_graphs; i++) {
      _ids[i] = UNDEFINED_IDS;
    }
    _graphs = new int[_num_graphs * numPoints];
    for (int i = 0; i < _num_graphs * _num_points; i++) {
      _graphs[i] = UNDEFINED_PLANT_VALUE;
    }

    // минимальная длина подписи - 4 символа, поэтому чаще подписи не поставить
    // поэтому если graphLengt меньше 8, то одна подпись, если 40 - 5
    graphXLabels = max(1, graphLength / 8);
  }

  ~Graph() {
    delete[] _ids;
    delete[] _graphs;
  }

  void addPoint(int plantId, int pointId, uint16_t pointValue) {
    int graphId = UNDEFINED_IDS;
    for (int i = 0; i < _num_graphs; i++) {
      if (_ids[i] == UNDEFINED_IDS) {
        graphId = i;
        _ids[i] = plantId;
        break;
      }
      if (_ids[i] == plantId) {
        graphId = i;
        break;
      }
    }

    if (graphId != UNDEFINED_IDS) {
      _graphs[graphId * _num_points + pointId] = pointValue;
    } else {
      log.print(F("!!!!!!!!!!!   graphId == -1 for "));
      log.println(plantId);
    }
  }

  String plot() {
    String result;
    for (int k = 0; k < _num_graphs; k++) {
      int finalGraph[graphLength];

      int minValue = 1024;
      int maxValue = 0;
      // вычисляем значения графика
      int x = 0;
      double step = (double)graphLength / _num_points;
      double value = 0;
      int ctn = 0;

      for (int i = 0; i < _num_points; i++) {
        value += _graphs[k * _num_points + i];
        ctn++;

        // если следующая точка будет генерировать новую точку графика либо
        // вообще текущая последняя скидываем точку
        if ((x <= (i + 1) * step && x < graphLength - 1) ||
            i == _num_points - 1) {
          int finalValue = (int)(value / ctn);
          maxValue = max(maxValue, finalValue);
          minValue = min(minValue, finalValue);
          finalGraph[x] = finalValue;

          value = 0;
          ctn = 0;
          x++;
        }
      }

      char out[graphHeight][graphLength];
      for (int i = 0; i < graphHeight; i++) {
        for (int j = 0; j < graphLength; j++) {
          out[i][j] = ' ';
        }
      }
      int stepY = max(1, (maxValue - minValue) / graphHeight);
      for (int i = 0; i < graphLength; i++) {
        int j = min((finalGraph[i] - minValue) / stepY, graphHeight - 1);
        out[j][i] = '*';
      }

      // просто рисуем график
      result += F("plant number ");
      result += _ids[k];
      result += F("\n```\n");

      for (int height = graphHeight - 1; height >= 0; height--) {
        int graphY = minValue + stepY * height + stepY / 2;
        int add = 4 - len(graphY);
        for (int i = 0; i < add; i++) {
          result += ' ';
        }
        result += graphY;
        result += '|';
        for (char c : out[height]) {
          result += c;
        }
        result += '\n';
      }

      result += F("    |");
      for (int i = 0; i < graphLength; i++) {
        result += '-';
      }

      result += F("\n     ");
      int baseId = 1 - graphLength;
      int xLabelStep = graphLength / graphXLabels;
      for (int i = 0; i < graphXLabels; i++) {
        int value = baseId + xLabelStep * i;
        int add = xLabelStep - 1 - len(value);
        result += value;
        for (int i = 0; i < add; i++) {
          result += ' ';
        }
      }
      result += F("\n```\n");
    }
    return result;
  }

 private:
  int _num_points;
  int _num_graphs;
  int* _graphs;
  int* _ids;
  Print& log;

  int graphHeight = 10;
  int graphLengthMax = 40;
  int graphLength;
  int graphXLabels;

  int len(int value) {
    if (value > 999) {
      return 4;
    } else if (value > 99) {
      return 3;
    } else if (value > 9) {
      return 2;
    } else {
      return 1;
    }
  }
};

#define MEDIAN_NUM_POINTS 7

class PointsHoler {
 public:
  PointsHoler(Print& logger) : log(logger) { clear(); }

  ~PointsHoler() {}

  void addPoint(uint8_t graphId, uint16_t value) {
    // добавили точку в массив
    _graphs[graphId * MEDIAN_NUM_POINTS + _ids[graphId]] = value;
    _ids[graphId]++;
    // если массив переполнен, очистили и сдампили значение
    if (_ids[graphId] == MEDIAN_NUM_POINTS) {
      uint16_t median = getMedian(graphId);
      _avg[graphId] += median;
      _ctn[graphId]++;
      _ids[graphId] = 0;
      log.print(F("Got median "));
      log.print(median);
      log.print(F("; ctn = "));
      log.print(_ctn[graphId]);
      log.print(F(", avg = "));
      log.println(_avg[graphId]);
    }
  }

  void dumpAvg(std::function<void(uint8_t x, uint16_t y)> callback) {
    for (uint8_t i = 0; i < PLANTS_AMOUNT; i++) {
      uint16_t avg;
      if (_ctn[i] > 0) {
        avg = (uint16_t)(_avg[i] / _ctn[i]);
      } else if (_ids[i] > 0) {
        avg = getMedian(i);
      } else {
        // нет собранных значений по данному растению
        // avg = UNDEFINED_PLANT_VALUE;
        continue;
      }
      callback(i, avg);
    }

    clear();
  }

 private:
  int _ids[PLANTS_AMOUNT];
  int _graphs[PLANTS_AMOUNT * MEDIAN_NUM_POINTS];
  float _avg[PLANTS_AMOUNT];
  int _ctn[PLANTS_AMOUNT];
  Print& log;

  void clear() {
    for (uint8_t i = 0; i < PLANTS_AMOUNT; i++) {
      _ids[i] = 0;
      _avg[i] = 0;
      _ctn[i] = 0;
    }
  }

  uint16_t getMedian(uint8_t graphId) {
    if (_ids[graphId] == 0) {
      return UNDEFINED_PLANT_VALUE;
    }
    int start = graphId * MEDIAN_NUM_POINTS;
    if (_ids[graphId] < 3) {
      return _graphs[start];
    }
    // сортировка массива
    for (int i = start; i < start + _ids[graphId] - 1; i++) {
      for (int j = i + 1; j < start + _ids[graphId]; j++) {
        if (_graphs[j] < _graphs[i]) {
          uint16_t swap = _graphs[j];
          _graphs[j] = _graphs[i];
          _graphs[i] = swap;
        }
      }
    }
    // выдаем медианное значение
    return _graphs[start + _ids[graphId] / 2];
  }
};