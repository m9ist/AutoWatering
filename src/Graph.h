#include <Arduino.h>
#include <State.h>
#define UNDEFINED_IDS -1

class Graph {
 public:
  Graph(int numGraphs, int numPoints)
      : _num_graphs(numGraphs), _num_points(numPoints) {
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

    _graphs[graphId * _num_points + pointId] = pointValue;
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
      int stepY = (maxValue - minValue) / graphHeight;
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